#!/usr/bin/env python3
"""Real-socket LB checks for relay backpressure, close, distribution and failover."""

import hashlib
import http.client
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time


server_binary = pathlib.Path(
    sys.argv[1] if len(sys.argv) > 1 else "build/perf-rest/aorta"
).resolve()
lb_binary = pathlib.Path(
    sys.argv[2] if len(sys.argv) > 2 else "build/perf-rest/aorta_lb"
).resolve()
fixture = pathlib.Path("public/aorta-lb-check.bin")
data = bytes(range(256)) * 8192
fixture.write_bytes(data)
servers = []
lb = None


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def start_lb(directory, backends):
    config = pathlib.Path(directory) / "config"
    config.mkdir(exist_ok=True)
    (config / "backends.conf").write_text(
        "".join(f"127.0.0.1:{port}\n" for port in backends)
    )
    return subprocess.Popen(
        [str(lb_binary)],
        cwd=directory,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env={"AORTA_LB_REACTORS": "2"},
    )


def request(port, path="/hello", close=False):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    headers = {"Connection": "close"} if close else {}
    connection.request("GET", path, headers=headers)
    response = connection.getresponse()
    body = response.read()
    status = response.status
    connection.close()
    return status, body


def request_count(port):
    body = request(port, "/metrics")[1].decode()
    return int(re.search(r"^aorta_requests_total (\d+)$", body, re.MULTILINE)[1])


hello = b"GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n"

try:
    for port in (18201, 18202):
        servers.append(
            subprocess.Popen(
                [str(server_binary), str(port)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        )
    time.sleep(0.7)

    with tempfile.TemporaryDirectory(prefix="aorta-lb-test-") as directory:
        lb = start_lb(directory, [18201, 18202])
        time.sleep(0.7)

        connection = http.client.HTTPConnection("127.0.0.1", 9000, timeout=10)
        for _ in range(10):
            connection.request("GET", "/hello")
            response = connection.getresponse()
            assert response.status == 200
            assert response.read() == b"Hello from AORTA"
        connection.close()

        with socket.create_connection(("127.0.0.1", 9000), timeout=5) as client:
            client.sendall(hello[:8])
            time.sleep(0.02)
            client.sendall(hello[8:] + hello)
            wire = b""
            while wire.count(b"Hello from AORTA") < 2:
                wire += client.recv(65536)
            assert wire.count(b"HTTP/1.1 200 OK") == 2

        before = [request_count(18201), request_count(18202)]
        for _ in range(80):
            assert request(9000, close=True) == (200, b"Hello from AORTA")
        after = [request_count(18201), request_count(18202)]
        distribution = [last - first for first, last in zip(before, after)]
        assert min(distribution) >= 30, distribution

        with socket.create_connection(("127.0.0.1", 9000), timeout=5) as client:
            client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 32768)
            client.sendall(
                b"GET /aorta-lb-check.bin HTTP/1.1\r\nHost: localhost\r\n\r\n"
            )
            time.sleep(0.25)
            response = http.client.HTTPResponse(client)
            response.begin()
            body = response.read()
            assert response.status == 200
            assert hashlib.sha256(body).digest() == hashlib.sha256(data).digest()

        client = socket.create_connection(("127.0.0.1", 9000), timeout=5)
        client.sendall(
            b"GET /aorta-lb-check.bin HTTP/1.1\r\nHost: localhost\r\n\r\n"
        )
        client.close()
        time.sleep(0.1)
        assert request(9000) == (200, b"Hello from AORTA")

        stop(lb)
        lb = start_lb(directory, [18200, 18201])
        time.sleep(0.5)
        assert request(9000) == (200, b"Hello from AORTA")

        deadline = time.monotonic() + 12
        unhealthy = False
        while time.monotonic() < deadline:
            time.sleep(0.5)
            body = request(9000, "/metrics")[1].decode()
            if re.search(
                r'aorta_lb_backend_healthy\{backend="127.0.0.1",port="18200"\} 0',
                body,
            ):
                unhealthy = True
                break
        assert unhealthy

    print(
        "PASS: keep-alive, fragmented/pipelined relay, connection close, "
        "2 MiB slow reader, client disconnect, distribution, failover, health checks"
    )
finally:
    stop(lb)
    for server in servers:
        stop(server)
    fixture.unlink(missing_ok=True)
