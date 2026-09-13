#!/usr/bin/env python3
"""Repeatable direct/LB/process-scaling benchmark with per-component CPU data."""

import argparse
import json
import os
import pathlib
import re
import resource
import signal
import subprocess
import time
import urllib.request


def ticks(pid):
    fields = pathlib.Path(f"/proc/{pid}/stat").read_text().split()
    return sum(map(int, fields[13:15]))


def metric(port, name):
    try:
        body = urllib.request.urlopen(
            f"http://127.0.0.1:{port}/metrics", timeout=2
        ).read().decode()
    except Exception:
        return None
    match = re.search(rf"^{re.escape(name)}\s+([0-9.]+)$", body, re.MULTILINE)
    return float(match.group(1)) if match else None


parser = argparse.ArgumentParser()
parser.add_argument("label")
parser.add_argument("--server-binary", default="build/perf-rest/aorta")
parser.add_argument("--lb-binary", default="build/perf-rest/aorta_lb")
parser.add_argument("--backends", type=int, default=1)
parser.add_argument("--via-lb", action="store_true")
parser.add_argument("--shared-port", action="store_true")
parser.add_argument("--path", default="/hello")
parser.add_argument("--concurrency", default="100,500,1000,5000")
parser.add_argument("--repeats", type=int, default=3)
parser.add_argument("--duration", default="6s")
parser.add_argument("--warmup", default="2s")
parser.add_argument("--server-cpus", default="0-3")
parser.add_argument("--lb-cpus", default="4-7")
parser.add_argument("--client-cpus", default="8-15")
parser.add_argument("--lb-reactors", default="2")
parser.add_argument("--redis-port")
parser.add_argument("--redis-pid", type=int)
parser.add_argument("--reactors")
parser.add_argument("--workers")
args = parser.parse_args()

root = pathlib.Path.cwd()
out = root / "benchmarks/results" / args.label
out.mkdir(parents=True, exist_ok=False)
(out / "config").mkdir()
server_binary = str((root / args.server_binary).resolve())
lb_binary = str((root / args.lb_binary).resolve())
ports = [18100 if args.shared_port else 18100 + i for i in range(args.backends)]
(out / "config/backends.conf").write_text(
    "".join(f"127.0.0.1:{port}\n" for port in ports)
)

environment = {**os.environ, "AORTA_REDIS_HOST": "127.0.0.1"}
if args.redis_port:
    environment["AORTA_REDIS_PORT"] = args.redis_port
if args.reactors:
    environment["AORTA_REACTORS"] = args.reactors
if args.workers:
    environment["AORTA_WORKERS"] = args.workers

servers = []
logs = []
lb = None
try:
    for index, port in enumerate(ports):
        log = open(out / f"server-{index + 1}.log", "w")
        logs.append(log)
        servers.append(
            subprocess.Popen(
                ["taskset", "-c", args.server_cpus, server_binary, str(port)],
                stdout=log,
                stderr=subprocess.STDOUT,
                env=environment,
            )
        )
    if args.via_lb:
        lb_log = open(out / "lb.log", "w")
        logs.append(lb_log)
        lb = subprocess.Popen(
            ["taskset", "-c", args.lb_cpus, lb_binary],
            cwd=out,
            stdout=lb_log,
            stderr=subprocess.STDOUT,
            env={**environment, "AORTA_LB_REACTORS": args.lb_reactors},
        )
    time.sleep(1)
    target_port = 9000 if args.via_lb else ports[0]
    url = f"http://127.0.0.1:{target_port}{args.path}"
    urllib.request.urlopen(url, timeout=3).read()
    records = []
    for repeat in range(1, args.repeats + 1):
        for concurrency in map(int, args.concurrency.split(",")):
            stem = out / f"r{repeat}-c{concurrency}"
            base_command = [
                "taskset", "-c", args.client_cpus, "wrk", "-t8",
                f"-c{concurrency}", "--timeout", "5s", "--latency", url,
            ]
            subprocess.run(
                base_command[:6] + [f"-d{args.warmup}"] + base_command[6:],
                stdout=open(str(stem) + ".warmup", "w"),
                stderr=subprocess.STDOUT,
                check=True,
            )
            pids = [process.pid for process in servers]
            if lb is not None:
                pids.append(lb.pid)
            if args.redis_pid:
                pids.append(args.redis_pid)
            before_ticks = {pid: ticks(pid) for pid in pids}
            before_requests = [
                metric(port, "aorta_requests_total") for port in ports
            ]
            start = time.monotonic()
            usage_start = resource.getrusage(resource.RUSAGE_CHILDREN)
            with open(str(stem) + ".wrk", "w") as wrk_output:
                client = subprocess.Popen(
                    base_command[:6] + [f"-d{args.duration}"] + base_command[6:],
                    stdout=wrk_output,
                    stderr=subprocess.STDOUT,
                )
                client.wait()
            elapsed = time.monotonic() - start
            usage_end = resource.getrusage(resource.RUSAGE_CHILDREN)
            client_cpu_seconds = (
                usage_end.ru_utime - usage_start.ru_utime
                + usage_end.ru_stime - usage_start.ru_stime
            )
            after_ticks = {pid: ticks(pid) for pid in pids}
            after_requests = [
                metric(port, "aorta_requests_total") for port in ports
            ]
            hz = os.sysconf("SC_CLK_TCK")
            component_cpu = [
                100 * (after_ticks[pid] - before_ticks[pid]) / hz / elapsed
                for pid in pids
            ]
            record = {
                "repeat": repeat,
                "concurrency": concurrency,
                "elapsed": elapsed,
                "server_cpu_percent": component_cpu[: len(servers)],
                "lb_cpu_percent": (
                    component_cpu[len(servers)] if lb is not None else None
                ),
                "redis_cpu_percent": (
                    component_cpu[-1] if args.redis_pid else None
                ),
                "backend_request_delta": [
                    None if first is None or last is None else last - first
                    for first, last in zip(before_requests, after_requests)
                ],
                "client_cpu_percent": 100 * client_cpu_seconds / elapsed,
            }
            pathlib.Path(str(stem) + ".json").write_text(json.dumps(record, indent=2))
            records.append(record)
            print(args.label, repeat, concurrency, component_cpu, flush=True)
    (out / "metadata.json").write_text(
        json.dumps({**vars(args), "records": records}, indent=2)
    )
finally:
    if lb is not None:
        lb.send_signal(signal.SIGTERM)
        try:
            lb.wait(timeout=4)
        except subprocess.TimeoutExpired:
            lb.kill()
            lb.wait()
    for server in servers:
        server.send_signal(signal.SIGTERM)
    for server in servers:
        try:
            server.wait(timeout=4)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
    for log in logs:
        log.close()
