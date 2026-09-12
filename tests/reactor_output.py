#!/usr/bin/env python3
"""Socket-level regression checks for immediate writes and EPOLLOUT fallback."""
import hashlib, http.client, os, pathlib, socket, subprocess, sys, time
binary=sys.argv[1] if len(sys.argv)>1 else 'build/perf-investigation/aorta'
port=18081
fixture=pathlib.Path('public/aorta-perf-check.bin')
assert not fixture.exists()
data=bytes(range(256))*24576  # 6 MiB: larger than a normal socket send buffer.
fixture.write_bytes(data)
s=subprocess.Popen([binary,str(port)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env={**os.environ,'AORTA_REDIS_HOST':'127.0.0.1'})
def connect():
    c=socket.create_connection(('127.0.0.1',port),timeout=5); c.settimeout(10); return c
hello=b'GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n'
try:
    time.sleep(.5)
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    for _ in range(10):
        c.request('GET','/hello');r=c.getresponse();assert r.status==200; assert r.read()==b'Hello from AORTA'
    c.request('HEAD','/hello');r=c.getresponse();assert r.status==200 and r.read()==b''
    c.close()
    # Fragmented request, then keep-alive close after a second response.
    with connect() as c:
        c.sendall(hello[:17]);time.sleep(.02);c.sendall(hello[17:])
        r=http.client.HTTPResponse(c);r.begin();assert r.read()==b'Hello from AORTA'
        c.sendall(hello.replace(b'Host:',b'Connection: close\r\nHost:'))
        r=http.client.HTTPResponse(c);r.begin();assert r.read()==b'Hello from AORTA';assert c.recv(1)==b''
    # Read the complete wire stream so buffered pipelined responses are preserved.
    with connect() as c:
        c.sendall(hello+hello.replace(b'Host:',b'Connection: close\r\nHost:'))
        wire=b''
        while chunk:=c.recv(65536):wire+=chunk
        assert wire.count(b'HTTP/1.1 200 OK')==2 and wire.count(b'Hello from AORTA')==2
    # Pause reads to force queued file output and EPOLLOUT resumption, then reuse fd.
    with connect() as c:
        c.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,65536)
        c.sendall(b'GET /aorta-perf-check.bin HTTP/1.1\r\nHost: localhost\r\n\r\n')
        time.sleep(.3)
        r=http.client.HTTPResponse(c);r.begin();body=r.read()
        assert r.status==200 and hashlib.sha256(body).digest()==hashlib.sha256(data).digest()
        c.sendall(hello);r=http.client.HTTPResponse(c);r.begin();assert r.read()==b'Hello from AORTA'
    # Both success and unavailable-Redis responses take the asynchronous callback path.
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
    for _ in range(3):
        c.request('GET','/tasks');r=c.getresponse();assert r.status==int(os.environ.get('AORTA_EXPECT_TASK_STATUS','503'));r.read()
        c.request('GET','/hello');r=c.getresponse();assert r.status==200;r.read()
    c.close()
    assert s.poll() is None
    print('PASS: keep-alive, HEAD, fragmented input, pipelining, close, 6 MiB slow reader, async completion')
finally:
    s.terminate()
    try:s.wait(timeout=5)
    except subprocess.TimeoutExpired:s.kill();s.wait()
    fixture.unlink()
