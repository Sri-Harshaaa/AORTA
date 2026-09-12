#!/usr/bin/env bash
set -euo pipefail
label=$1
binary=$2
# A disposable Redis with no host database mounts and no published network port.
name="aorta-perf-redis-$$"
cleanup() { docker rm -f "$name" >/dev/null; }
trap cleanup EXIT
docker run -d --name "$name" --network host --cpuset-cpus 8-15 redis:7-alpine redis-server --bind 127.0.0.1 --port 16379 --save '' --appendonly no >/dev/null
docker exec "$name" redis-cli -p 16379 EVAL "for i=1,100 do redis.call('SADD','tasks',i); redis.call('HSET','task:'..i,'title','Benchmark task '..i,'completed','0'); end; return 100" 0 >/dev/null
export AORTA_REDIS_PORT=16379
python3 benchmarks/run.py "$label-tasks" --binary "$binary" --path /tasks --repeats 1 --duration 10s
python3 benchmarks/run.py "$label-lb" --binary "$binary" --lb --repeats 1 --duration 10s
