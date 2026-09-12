#!/usr/bin/env python3
"""Repeatable local HTTP baseline; raw wrk, process/thread and per-core CPU logs."""
import argparse, json, os, pathlib, signal, subprocess, time
p=argparse.ArgumentParser(); p.add_argument('label'); p.add_argument('--binary',default='build/perf-investigation/aorta'); p.add_argument('--repeats',type=int,default=3); p.add_argument('--duration',default='15s'); p.add_argument('--concurrency',default='100,500,1000,5000'); p.add_argument('--path',default='/hello'); p.add_argument('--lb',action='store_true'); p.add_argument('--client-threads',type=int,default=8); a=p.parse_args()
out=pathlib.Path('benchmarks/results')/a.label; out.mkdir(parents=True,exist_ok=True)
url='http://127.0.0.1:'+('9000' if a.lb else '18080')+a.path
server=subprocess.Popen(['taskset','-c','0-7',a.binary,'18080'],stdout=open(out/'server.log','w'),stderr=subprocess.STDOUT,env={**os.environ,'AORTA_REDIS_HOST':'127.0.0.1'})
(out/'pid').write_text(str(server.pid))
lb=None
if a.lb:
    (out/'config').mkdir(exist_ok=True)
    (out/'config/backends.conf').write_text('127.0.0.1:18080\n')
    lb=subprocess.Popen(['taskset','-c','0-7',str(pathlib.Path('build/perf-investigation/aorta_lb').resolve())],cwd=out,stdout=open(out/'lb.log','w'),stderr=subprocess.STDOUT,env={**os.environ,'AORTA_LB_REACTORS':'4'})
def runwrk(c,d,path):
    cmd=['taskset','-c','8-15','wrk',f'-t{a.client_threads}',f'-c{c}',f'-d{d}','--timeout','5s','--latency',url]
    with open(path,'w') as f: return subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
def ticks():
    s=pathlib.Path(f'/proc/{server.pid}/stat').read_text().split(); return sum(map(int,s[13:15]))
try:
    time.sleep(1)
    subprocess.run(['curl','-fsS',url],check=True,stdout=open(out/'response','w'))
    for r in range(1,a.repeats+1):
        for c in map(int,a.concurrency.split(',')):
            stem=out/f'r{r}-c{c}'
            runwrk(c,'3s',str(stem)+'.warmup')
            monitors=[]
            for cmd,suffix in [(['pidstat','-u','-r','-w','-t','-p',str(server.pid),'1'],'.pidstat'),(['mpstat','-P','ALL','1'],'.mpstat')]:
                monitors.append(subprocess.Popen(cmd,stdout=open(str(stem)+suffix,'w'),stderr=subprocess.STDOUT))
            start=time.monotonic(); cpu=ticks()
            runwrk(c,a.duration,str(stem)+'.wrk')
            elapsed=time.monotonic()-start; used=(ticks()-cpu)/os.sysconf('SC_CLK_TCK')
            for m in monitors: m.terminate(); m.wait()
            data={'elapsed':elapsed,'server_cpu_seconds':used,'server_cpu_percent':100*used/elapsed,'concurrency':c,'repeat':r}
            pathlib.Path(str(stem)+'.json').write_text(json.dumps(data,indent=2))
            print(a.label,r,c,round(data['server_cpu_percent'],1),flush=True)
finally:
    if lb is not None:
        lb.terminate(); lb.wait(timeout=5)
    server.terminate()
    try: server.wait(timeout=5)
    except subprocess.TimeoutExpired: server.kill(); server.wait()
