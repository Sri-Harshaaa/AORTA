# AORTA

A custom Layer-4 TCP load balancer and a backend HTTP server, written from
scratch in C++20 on Linux sockets, built to measure what actually happens to a
server as concurrency climbs.

The server ships **two concurrency models behind the same HTTP code** — an
epoll event loop and a thread-per-connection design — so the comparison between
them isolates the concurrency model rather than the parser or the router.

> **Linux only.** This uses `epoll`, `accept4`, `timerfd`, `sendfile`,
> `SO_REUSEPORT` and `/proc`. It does not build on macOS or Windows. On Windows,
> use WSL2 or Docker.

---

## Architecture

```
                       clients
                          |
                  ┌───────▼────────┐
                  │    aorta_lb    │  :9000   single-threaded epoll relay
                  │  round_robin / │          health checks, backpressure,
                  │  least_conns / │          /metrics
                  │  consistent_h  │
                  └───┬────┬───┬───┘
                      │    │   │
           ┌──────────┘    │   └──────────┐
     ┌─────▼─────┐   ┌─────▼─────┐  ┌─────▼─────┐
     │  aorta    │   │  aorta    │  │  aorta    │   :8081-8083
     │  N reactors│  │ N reactors│  │ N reactors│   one per core,
     └─────┬─────┘   └─────┬─────┘  └─────┬─────┘   SO_REUSEPORT
           └───────────────┼──────────────┘
                     ┌─────▼─────┐
                     │   redis   │  one connection per reactor
                     └───────────┘
```

The backend serves a small task API plus a static dashboard, which exists to
give the benchmark a route that does real work (a Redis round trip) alongside
routes that do none.

| Route | Method | Notes |
|---|---|---|
| `/hello` | GET | Constant response. Use this to measure the server itself. |
| `/health` | GET | Liveness, used by the balancer's health checker. |
| `/metrics` | GET | Prometheus exposition, aggregated across all workers. |
| `/tasks` | GET, POST | Redis-backed. Use this to measure the backend path. |
| `/tasks/{id}` | PUT, DELETE | Redis-backed. |
| `/` and `*.css`, `*.js` … | GET, HEAD | Static files from `public/`. |

---

## Build

```bash
sudo apt-get install -y build-essential cmake libhiredis-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

ctest --test-dir build --output-on-failure
```

Two binaries land in `build/`: `aorta` (backend) and `aorta_lb` (balancer).

### Run with Docker

```bash
docker compose up --build

curl localhost:9000/hello
curl localhost:9000/metrics          # balancer metrics
open  http://localhost:9000/         # dashboard
```

Switch either dimension without editing files:

```bash
AORTA_MODE=threaded docker compose up --build
AORTA_LB_STRATEGY=least_connections docker compose up --build
```

### Run locally

```bash
redis-server &

./build/aorta --port 8081 --redis-host 127.0.0.1 &
./build/aorta --port 8082 --redis-host 127.0.0.1 &
./build/aorta --port 8083 --redis-host 127.0.0.1 &

./build/aorta_lb --port 9000 --strategy round_robin
```

---

## Options

**`aorta`** — backend server

| Flag | Default | |
|---|---|---|
| `--port <n>` | 8080 | Listen port |
| `--mode <epoll\|threaded>` | epoll | Concurrency model |
| `--workers <n>` | CPU count | Reactors, epoll mode only |
| `--max-threads <n>` | 4096 | Connection cap, threaded mode only |
| `--backlog <n>` | 4096 | `listen()` backlog |
| `--redis-host <h>` | redis | |
| `--redis-port <n>` | 6379 | |
| `--redis-pool <n>` | 32 | Pool size, threaded mode only |
| `--public <dir>` | ./public | Static file root |

Environment equivalents: `AORTA_PORT`, `AORTA_MODE`, `AORTA_WORKERS`,
`AORTA_REDIS_HOST`, `AORTA_REDIS_PORT`, `AORTA_REDIS_POOL`.

**`aorta_lb`** — load balancer

| Flag | Default | |
|---|---|---|
| `--port <n>` | 9000 | |
| `--strategy <name>` | round_robin | `round_robin`, `least_connections`, `consistent_hash` |
| `--backends <path>` | config/backends.conf | |
| `--health-interval <n>` | 5 | Seconds between checks |
| `--max-connections <n>` | 100000 | |
| `--max-buffer <bytes>` | 8388608 | Per-direction relay cap |
| `--max-total-buffer <bytes>` | 536870912 | Process-wide relay budget |

Environment equivalents: `AORTA_LB_PORT`, `AORTA_LB_BACKENDS`,
`AORTA_LB_STRATEGY`.

---

## Load-balancing strategies

**`round_robin`** (default) — hands out backends in order, skipping unhealthy
ones. The only strategy whose distribution does not depend on where clients
come from, which is why it is the default and the right choice for benchmarking.

**`least_connections`** — picks the healthy backend currently holding the
fewest connections. Ties rotate, so an idle pool still fans out instead of
piling onto backend 0.

**`consistent_hash`** — an FNV-1a hash ring with 128 virtual nodes per backend.
Gives stable client-to-backend affinity and remaps only the departing backend's
share when membership changes.

> **Benchmarking hazard.** Consistent hashing routes on the client's source IP.
> Every connection from one load generator carries the same IP, so the whole
> test lands on a single backend and the balancer appears to distribute nothing.
> This is correct affinity behaviour, not a bug — but it makes the strategy
> useless for throughput tests. Demonstrate it instead by killing a backend and
> showing that the other clients' routing does not move. There is a test pinning
> this behaviour in `tests/test_strategies.cpp`.

---

## Benchmarking

```bash
# Inspect, then apply host tuning (needs root).
./bench/tune.sh --show
sudo ./bench/tune.sh
ulimit -n 1048576

./bench/run_bench.sh --label epoll    --url http://127.0.0.1:9000/hello
AORTA_MODE=threaded docker compose up -d --build
./bench/run_bench.sh --label threaded --url http://127.0.0.1:9000/hello

./bench/compare.sh bench/results/epoll.csv bench/results/threaded.csv
```

The sweep runs 100 → 500 → 1,000 → 5,000 → 10,000 → 50,000 connections and
writes a CSV row per step with throughput, average latency, P50/P95/P99/P99.9,
socket errors, RSS, CPU and open descriptors. **A step that cannot complete is
recorded as the observed ceiling** rather than skipped, because where the
system stops is the result.

### Why wrk2 and not wrk or ab

`run_bench.sh` requires **wrk2**. A standard `wrk` run issues the next request
only after the previous one returns, so while the server is stalled the client
stops taking measurements and the stall never enters the latency distribution.
That is coordinated omission, and it can improve a reported P99 by an order of
magnitude. wrk2 holds a constant offered rate and accounts for the requests it
should have sent, which is the only way a tail-latency comparison between two
concurrency models means anything.

### Things that will cap you before the server does

- **Client ephemeral ports.** One host can hold roughly 64,000 simultaneous
  connections to a single destination, from the port range alone. Past that you
  need more client hosts or more destination addresses. Hitting this wall is not
  the server failing.
- **`ulimit -n`.** Both ends need descriptors; the balancer needs two per
  connection because it holds both sides.
- **`net.core.somaxconn`.** The kernel clamps `listen()`'s backlog to this, so
  raising the backlog in code alone does nothing.
- **TIME_WAIT.** The sweep sleeps between steps to let sockets drain;
  `tune.sh` enables `tcp_tw_reuse`.
- **Docker Desktop.** On a virtualised host you are partly measuring the
  hypervisor. Benchmark on real Linux, or at least say which you used.

---

## Metrics

Both binaries expose Prometheus text on `/metrics`.

The server reports `aorta_requests_total`, `aorta_responses_total`,
`aorta_errors_total`, `aorta_active_connections`, `aorta_requests_per_second`,
a full `aorta_request_duration_seconds` histogram with P50/P95/P99/P99.9
gauges, plus `aorta_process_cpu_percent`,
`aorta_process_resident_memory_bytes`, `aorta_process_open_fds` and
`aorta_process_threads`. Per-worker counters are broken out under
`aorta_worker_*`, because the way `SO_REUSEPORT` splits accepts across reactors
is uneven and worth seeing.

Latency is recorded from **the first byte of a request arriving to its response
being serialized and queued.** That covers parsing, routing and any Redis round
trip. It deliberately excludes draining the response to a slow client, which
the server does not control — end-to-end time is what the load generator
measures.

The histogram uses fixed logarithmic buckets, four per power of two, which
bounds the error on a reported percentile at 1/8 and makes recording a single
relaxed atomic increment with no allocation. Reported percentiles round up, so
they never understate latency.

---

## Design notes

**One epoll reactor per core, not one thread per connection.** Each reactor has
its own listening socket via `SO_REUSEPORT` and the kernel spreads accepts. This
is the nginx model.

**One Redis connection per reactor.** A single shared connection behind a mutex
would serialize every reactor onto one core the moment a request touched Redis,
which would make an epoll server behave like a blocking one. The threaded model
uses a bounded pool instead, since it can have thousands of threads live and
cannot open a connection per thread.

**The remaining bottleneck is honest and documented:** hiredis is used
synchronously, so a `/tasks` request still parks its own reactor for the
duration of the round trip. It no longer blocks the *other* reactors. Moving to
the hiredis async API driven by the existing `Epoll` would remove the last
stall; until then, benchmark `/hello` to measure the server and `/tasks` to
measure the server plus this bottleneck. The difference between those two
numbers is itself the interesting result.

**Real backpressure in the balancer.** Relay buffers are accounted
process-wide. When a connection's pending bytes pass half the per-connection cap,
or the global budget is reached, `EPOLLIN` is dropped for that direction so the
kernel applies TCP backpressure to the sender instead of the balancer growing
memory without bound.

**The HTTP parser rejects request smuggling.** `Transfer-Encoding` together
with `Content-Length`, conflicting duplicate `Content-Length` headers, and any
request without exactly one `Host` are all refused. Chunked encoding and
trailers are supported, sizes are parsed with overflow guards, and the parser is
incremental — `tests/test_http_parser.cpp` feeds it a byte at a time to prove it.

---

## Repository layout

```
apps/            entry points for the two binaries
include/, src/
  net/           Socket, Epoll, TimerFd, Connection
  http/          parser, request, response, router, handler, static files
  server/        Reactor, Server, ThreadedServer, Metrics, Histogram, ProcStats
  lb/            LoadBalancer, strategies, health checker, backend pool
  task/          task store over Redis, plus a pooled variant
  redis/         hiredis wrapper
tests/           parser, histogram and strategy tests (ctest)
bench/           benchmark sweep, host tuning, comparison
public/          dashboard
config/          backend lists for local and Docker
```

---

## Status

Built and measured: the balancer, three strategies, async health checking, both
concurrency models, the metrics pipeline, and the benchmark harness.

Not done yet: the async Redis client described above, and a published set of
benchmark results — the harness exists, the numbers have to be produced on a
real Linux host.
