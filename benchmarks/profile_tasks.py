#!/usr/bin/env python3
"""Attribute the separate seeded Redis workload using Redis's own CPU/command counters."""
import os,pathlib,subprocess,time,json,sys
out=pathlib.Path('benchmarks/results/tasks-profile');out.mkdir(parents=True,exist_ok=True)
name=f'aorta-perf-redis-profile-{os.getpid()}'
def docker(*args):return subprocess.check_output(['docker',*args],text=True)
s=None
try:
    docker('run','-d','--name',name,'--network','host','--cpuset-cpus','8-15','redis:7-alpine','redis-server','--bind','127.0.0.1','--port','16379','--save','','--appendonly','no')
    docker('exec',name,'redis-cli','-p','16379','EVAL',"for i=1,100 do redis.call('SADD','tasks',i); redis.call('HSET','task:'..i,'title','Benchmark task '..i,'completed','0'); end; return 100",'0')
    s=subprocess.Popen(['taskset','-c','0-7','build/perf-investigation/aorta-before','18080'],env={**os.environ,'AORTA_REDIS_HOST':'127.0.0.1','AORTA_REDIS_PORT':'16379'},stdout=open(out/'server.log','w'),stderr=subprocess.STDOUT)
    time.sleep(1)
    for when in ['before','after']:
        (out/f'{when}.info').write_text(docker('exec',name,'redis-cli','-p','16379','INFO','all'))
        if when=='before':
            with open(out/'load.wrk','w') as f:
                subprocess.run(['taskset','-c','8-15','wrk','-t8','-c500','-d10s','--latency','http://127.0.0.1:18080/tasks'],stdout=f,check=True)
    with open(out/'correctness-redis.txt','w') as f:
        subprocess.run(['python3','tests/reactor_output.py'],env={**os.environ,'AORTA_REDIS_PORT':'16379','AORTA_EXPECT_TASK_STATUS':'200'},stdout=f,check=True)
finally:
    if s:
        s.terminate()
        try:s.wait(timeout=5)
        except subprocess.TimeoutExpired:s.kill();s.wait()
    docker('rm','-f',name)
