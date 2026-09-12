#!/usr/bin/env python3
import os,pathlib,subprocess,time,sys,signal,json
label=sys.argv[1]; binary=sys.argv[2] if len(sys.argv)>2 else 'build/perf-investigation/aorta'
out=pathlib.Path('benchmarks/results')/label; out.mkdir(parents=True,exist_ok=True)
s=subprocess.Popen(['taskset','-c','0-7',binary,'18080'],stdout=open(out/'server.log','w'),stderr=subprocess.STDOUT)
def load(c,d):
    return subprocess.Popen(['taskset','-c','8-15','wrk','-t8',f'-c{c}',f'-d{d}s','--latency','http://127.0.0.1:18080/hello'],stdout=open(out/f'load-{c}-{d}.wrk','w'))
try:
    time.sleep(1)
    for c in [100,5000]:
        w=load(c,12);time.sleep(2)
        subprocess.run(['perf','record','-F','199','-e','cycles:u','-g','--call-graph','fp','-p',str(s.pid),'-o',str(out/f'c{c}.data'),'--','sleep','8'],stderr=open(out/f'c{c}.perf-log','w'))
        # Each reactor epoll owns one listener, one timer, one completion fd; remaining fds are clients.
        counts={}
        for f in pathlib.Path(f'/proc/{s.pid}/fdinfo').iterdir():
            t=f.read_text(); n=t.count('tfd:')
            if n: counts[f.name]=n-3
        (out/f'c{c}-distribution.json').write_text(json.dumps(counts,indent=2))
        w.wait()
        with open(out/f'c{c}.report','w') as f:
            subprocess.run(['perf','report','--stdio','--no-children','-i',str(out/f'c{c}.data'),'--percent-limit','0.5'],stdout=f,stderr=subprocess.STDOUT)
    # Trace a short separate run: syscall timing is distorted, count ratios are useful.
    s.terminate(); s.wait(timeout=5)
    trace=subprocess.Popen(['strace','-f','-c','-o',str(out/'strace-summary.txt'),'taskset','-c','0-7',binary,'18080'],stdout=open(out/'trace-server.log','w'),stderr=open(out/'strace-log','w'),start_new_session=True)
    time.sleep(1); w=load(1000,5); w.wait(); os.killpg(trace.pid,signal.SIGTERM);trace.wait(timeout=10)
finally:
    s.terminate()
    try:s.wait(timeout=5)
    except subprocess.TimeoutExpired:s.kill();s.wait()
