#!/usr/bin/env python3
"""Task CRUD and listing check against an isolated Redis instance."""

import http.client
import json
import os
import pathlib
import subprocess
import sys
import time


binary = pathlib.Path(
    sys.argv[1] if len(sys.argv) > 1 else "build/perf-rest/aorta"
).resolve()
name = f"aorta-task-correctness-{os.getpid()}"
server = None


def call(method, path, body=None):
    connection = http.client.HTTPConnection("127.0.0.1", 18210, timeout=10)
    headers = {"Content-Type": "application/json"} if body is not None else {}
    payload = json.dumps(body) if body is not None else None
    connection.request(method, path, payload, headers)
    response = connection.getresponse()
    data = response.read()
    status = response.status
    connection.close()
    return status, data


try:
    subprocess.run(
        [
            "docker", "run", "-d", "--name", name, "--network", "host",
            "redis:7-alpine", "redis-server", "--bind", "127.0.0.1",
            "--port", "16379", "--save", "", "--appendonly", "no",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    server = subprocess.Popen(
        [str(binary), "18210"],
        env={
            **os.environ,
            "AORTA_REDIS_HOST": "127.0.0.1",
            "AORTA_REDIS_PORT": "16379",
            "AORTA_REACTORS": "2",
            "AORTA_WORKERS": "2",
        },
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.7)
    assert call("GET", "/tasks") == (200, b"[]")
    status, body = call("POST", "/tasks", {"title": "one"})
    assert status == 201
    task = json.loads(body)
    assert task == {"id": 1, "title": "one", "completed": False}
    assert json.loads(call("GET", "/tasks")[1]) == [task]
    status, body = call(
        "PUT", "/tasks/1", {"title": "updated", "completed": True}
    )
    assert status == 200
    task = json.loads(body)
    assert task == {"id": 1, "title": "updated", "completed": True}
    assert json.loads(call("GET", "/tasks")[1]) == [task]
    assert call("DELETE", "/tasks/1")[0] == 204
    assert call("GET", "/tasks") == (200, b"[]")
    print("PASS: empty list, create, native Redis listing, update, delete")
finally:
    if server is not None:
        server.terminate()
        try:
            server.wait(timeout=4)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
    subprocess.run(
        ["docker", "rm", "-f", name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
