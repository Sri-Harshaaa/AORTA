<div align="center">

# 🫀 AORTA
### **A Miniature Distributed System Built From Scratch**

*A systems-focused project exploring Linux networking, concurrency, load balancing, persistence, observability, failure handling, and measurement-driven performance engineering.*

<br>

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Redis](https://img.shields.io/badge/Redis-7-DC382D?style=for-the-badge&logo=redis&logoColor=white)
![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=for-the-badge&logo=docker&logoColor=white)
![Prometheus](https://img.shields.io/badge/Prometheus-Monitoring-E6522C?style=for-the-badge&logo=prometheus&logoColor=white)
![Grafana](https://img.shields.io/badge/Grafana-Dashboard-F46800?style=for-the-badge&logo=grafana&logoColor=white)

<br>

**Custom L4 TCP Load Balancer · Multi-Reactor HTTP/1.1 Servers · Worker Pool · Redis · Prometheus · Grafana**

</div>

---

## 🧠 What is AORTA?

AORTA is a **high-concurrency distributed server system** built to explore what happens underneath a modern backend stack instead of hiding networking and concurrency behind a framework.

The project contains:

- a custom **Layer-4 TCP load balancer**
- multi-reactor, event-driven **HTTP/1.1 servers**
- Linux `epoll`, non-blocking sockets and `SO_REUSEPORT`
- a custom incremental HTTP parser and router
- asynchronous worker execution
- Redis-backed task persistence
- health checks and failover
- Prometheus metrics and Grafana dashboards
- reproducible benchmark, profiling and regression-test tooling

The Task Manager is intentionally simple: it provides a real workload that can be served, persisted, distributed, monitored and stressed while the infrastructure remains the focus.

> **AORTA is built to understand the infrastructure underneath a web application — not just the application itself.**

---

# 🏗️ System Architecture

```text
                                      🌐 CLIENT
                                          │
                                          ▼
                              ┌───────────────────────┐
                              │      ⚡ AORTA LB       │
                              │                       │
                              │ Layer-4 TCP Proxy     │
                              │ Multi-Reactor / epoll │
                              │ Routing + Health      │
                              │ Failover              │
                              └───────────┬───────────┘
                                          │
                       ┌──────────────────┼──────────────────┐
                       │                  │                  │
                       ▼                  ▼                  ▼
                ┌────────────┐     ┌────────────┐     ┌────────────┐
                │  SERVER 1  │     │  SERVER 2  │     │  SERVER 3  │
                │   :8081    │     │   :8082    │     │   :8083    │
                │            │     │            │     │            │
                │  Reactor   │     │  Reactor   │     │  Reactor   │
                │  HTTP/1.1  │     │  HTTP/1.1  │     │  HTTP/1.1  │
                │ WorkerPool │     │ WorkerPool │     │ WorkerPool │
                └──────┬─────┘     └──────┬─────┘     └──────┬─────┘
                       │                  │                  │
                       └──────────────────┼──────────────────┘
                                          │
                                          ▼
                                   ┌─────────────┐
                                   │  🔴 Redis   │
                                   │ Task Store  │
                                   └─────────────┘

                    ┌──────────────────────────────────┐
                    │       📊 OBSERVABILITY           │
                    │                                  │
                    │ AORTA → Prometheus → Grafana    │
                    └──────────────────────────────────┘
```

The web server and load balancer are independently benchmarkable components. The load balancer is not required to measure the raw HTTP-server fast path.

---

# 🌐 Networking Layer

AORTA implements its networking path directly using Linux sockets.

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()/accept4()
   ↓
non-blocking socket
   ↓
epoll
   ↓
recv() / send()
   ↓
close()
```

Instead of assigning one thread to every connection, AORTA uses event-driven reactors:

```text
Many TCP Connections
         │
         ▼
    epoll Reactor
         │
         ▼
 ready sockets only
```

Each reactor owns its network-side connection state. Blocking application work is moved away from the event loop.

---

# 🌐 HTTP/1.1 Engine

AORTA includes its own lightweight HTTP processing layer.

```text
TCP Byte Stream
      │
      ▼
┌───────────────────────┐
│      HTTP Parser      │
│ Request Line          │
│ Headers               │
│ Body                  │
│ Chunked Encoding      │
│ Trailers              │
└───────────┬───────────┘
            │
            ▼
       HttpRequest
            │
            ▼
        HTTP Router
            │
            ▼
        HttpHandler
            │
            ▼
       HttpResponse
```

### Supported methods

| Method | Support |
|:---:|:---:|
| `GET` | ✅ |
| `POST` | ✅ |
| `PUT` | ✅ |
| `DELETE` | ✅ |
| `HEAD` | ✅ |
| `OPTIONS` | ✅ |

### Current routes

```text
GET    /hello
GET    /health
GET    /metrics
GET    /tasks

POST   /tasks
PUT    /tasks/:id
DELETE /tasks/:id
```

The server also supports static-file serving from the `public` directory.

---

# 🧵 Multi-Reactor Architecture

```text
                    Server
                      │
          ┌───────────┼───────────┐
          │           │           │
          ▼           ▼           ▼
      Reactor 1   Reactor 2   Reactor 3
          │           │           │
        epoll       epoll       epoll
          │           │           │
          └───────────┼───────────┘
                      │
                 Worker Pool
                      │
                    Redis
```

Runtime thread counts can be controlled explicitly for experiments without changing the defaults:

```bash
AORTA_REACTORS=<count>
AORTA_WORKERS=<count>
```

These controls are used by the scalability harness to distinguish reactor/process oversubscription from useful scaling.

---

# 🧵 Worker Pool & Asynchronous Redis

Redis operations are kept off the network reactor.

```text
Request
   │
   ▼
⚡ Reactor
   │ submit
   ▼
Worker Queue
   │
   ├────► Worker
   ├────► Worker
   └────► Worker
             │
             ▼
          🔴 Redis
             │
             ▼
          eventfd
             │
             ▼
         ⚡ Reactor
             │
             ▼
          Response
```

Each worker maintains its own Redis client connection. Completion is signaled back to the reactor through `eventfd`.

---

# 🌍 Layer-4 TCP Load Balancer

The AORTA load balancer operates at **Layer 4** and forwards TCP traffic without parsing the application protocol.

```text
               Layer 4                         Application Layer

Client ───── TCP ─────► AORTA LB ───── TCP ─────► Backend
                                                    │
                                                    ▼
                                               HTTP Parser
```

### Routing

AORTA supports:

- Round Robin
- Consistent Hashing with virtual nodes

The routing mode can be selected with:

```bash
AORTA_LB_ROUTING=round_robin
AORTA_LB_ROUTING=consistent_hash
```

### Health checks and failover

Unhealthy backends are removed from rotation and healthy backends continue receiving traffic. Health checking runs through the load balancer's event-driven loop.

Regression coverage includes:

- partial writes
- slow readers
- client disconnects
- backend disconnects
- backend distribution
- health checks
- failover
- connection churn

---

# 📝 Task Manager

The application workload exposes Redis-backed CRUD operations:

| Method | Endpoint | Description |
|:---:|:---|:---|
| `GET` | `/tasks` | List tasks |
| `POST` | `/tasks` | Create a task |
| `PUT` | `/tasks/:id` | Update a task |
| `DELETE` | `/tasks/:id` | Delete a task |

The frontend is available through the load balancer at:

```text
http://localhost:9000/
```

---

# 🚀 Redis Listing Optimization

The task-listing path went through two generations of optimization.

The earlier implementation avoided an N+1 network access pattern by moving task retrieval into Redis, but its Lua script still performed a per-task loop.

Profiling showed that this Lua execution became the dominant Redis cost under load.

The current path uses native Redis operations:

```text
GET /tasks
    │
    ▼
AORTA worker
    │
    ▼
Redis SORT ... GET
    │
    ▼
serialize response
```

In the controlled c1000 `/tasks` test:

| Metric | Before | After |
|---|---:|---:|
| Successful requests/sec | **3,258** | **9,593** |
| p99 latency | **462 ms** | **107 ms** |

That is approximately a **2.94× increase in successful throughput** with roughly a **77% reduction in p99 latency**.

At the current end-to-end limit, Redis becomes the first saturated component for this workload.

---

# ⚡ HTTP Fast-Path Optimization

Profiling found that every response previously scheduled an unnecessary writable-event cycle:

```text
OLD

response ready
    ↓
enable EPOLLOUT
    ↓
epoll_ctl
    ↓
epoll_wait
    ↓
writable event
    ↓
send
    ↓
disable EPOLLOUT
```

The optimized path attempts the write immediately and only subscribes to `EPOLLOUT` when the socket actually blocks:

```text
NEW

response ready
    ↓
send immediately
    │
    ├── complete ──► done
    │
    └── partial / EAGAIN
              ↓
        enable EPOLLOUT
              ↓
        resume later
```

Measured `epoll_ctl` calls per send fell from:

```text
2.012  →  0.00624
```

The same controlled before/after benchmark produced:

| Connections | Before RPS | After RPS | Mean latency before → after | p99 before → after |
|---:|---:|---:|---:|---:|
| 100 | 484,479 | **522,222** | 0.378 → **0.224 ms** | 2.63 → **1.75 ms** |
| 500 | 511,187 | **550,751** | 1.050 → **0.735 ms** | 4.30 → **3.20 ms** |
| 1000 | 480,011 | **550,497** | 2.000 → **1.240 ms** | 5.78 → **3.58 ms** |
| 5000 | 362,631 | **386,449** | 13.310 → **6.560 ms** | 24.64 → **13.58 ms** |

The partial-write and `EAGAIN` paths are regression-tested so the fast path does not trade correctness for throughput.

> These figures belong to this specific fixed-affinity direct-server experiment. Results from different benchmark topologies are intentionally reported separately.

---

# 🔀 Load-Balancer Capacity

The load balancer was profiled independently by comparing a backend directly against the same backend through the proxy.

At c1000 in the controlled direct-vs-proxy test:

| Path | Throughput |
|---|---:|
| Direct backend | **~223K RPS** |
| 4-reactor AORTA LB → backend | **~151K RPS** |

Adding two or three backend servers did **not** materially increase aggregate throughput in this fast-path workload, while request distribution remained essentially even.

This establishes the L4 proxy as the limiting component for this particular high-throughput workload rather than the backend-selection algorithm or an imbalanced server pool.

A connection-churn bug discovered during profiling was also fixed:

```text
LB read errors during churn:
62,386  →  0
```

---

# 📊 End-to-End Bottleneck Picture

AORTA does not have one universal bottleneck. The limiting component changes with workload.

### Fast HTTP workload

```text
Client
   │
   ▼
AORTA LB  ← saturates before multiple backends add useful capacity
   │
   ├── Server 1
   ├── Server 2
   └── Server 3
```

### Redis-backed `/tasks` workload

```text
Client
   │
   ▼
AORTA LB
   │
   ▼
AORTA Servers
   │
   ▼
Redis  ← saturates first
```

With three backends, end-to-end `/tasks` throughput is approximately:

```text
~9.5K successful requests/sec
```

At that point the load balancer remains below roughly **37% process CPU**, confirming that Redis—not the proxy—is the first saturated component for this workload.

---

# 📈 Same-Host Scaling Experiment

Multiple complete server processes were also tested on the same physical host:

| Processes | Aggregate throughput |
|---:|---:|
| 1 | **~347K RPS** |
| 2 | **~277K RPS** |
| 4 | **~243K RPS** |

Throughput decreased because additional full AORTA instances oversubscribed an architecture that already uses multiple reactors.

This is intentionally described as **same-host multicore/process scaling**, not horizontal scaling across machines.

The repository now exposes `AORTA_REACTORS` and `AORTA_WORKERS` so future tests can hold the total execution resources constant while comparing process-level and reactor-level scaling.

---

# 🔬 Performance Engineering Methodology

AORTA follows a measurement-first workflow:

```text
📏 Baseline
    ↓
🔎 Profile
    ↓
🎯 Confirm root cause
    ↓
🛠️ Make one targeted change
    ↓
🧪 Repeat identical benchmark
    ↓
✅ Keep measurable wins
↩️ Revert non-wins
```

Examples:

- redundant response-side `EPOLLOUT` cycles were measured and removed
- `epoll_ctl` calls/send fell from 2.012 to 0.00624
- Redis Lua per-task work was replaced with native `SORT ... GET`
- `/tasks` c1000 successful throughput improved from 3,258 to 9,593 req/s
- an LB peer-close bug causing 62,386 churn-time read errors was fixed
- a profiled hash-map optimization showed no measurable improvement and was **reverted**
- local `wrk`/network processing was identified as a measurement constraint at high direct-path load

This project deliberately reports rejected hypotheses and benchmark limitations instead of keeping changes simply because they appear theoretically faster.

---

# 🧪 Benchmark & Regression Tooling

The repository contains reproducible performance and correctness tooling under:

```text
benchmarks/
tests/
```

Important files include:

```text
benchmarks/
├── run.py
├── profile.py
├── profile_tasks.py
├── scalability.py
├── summarize.py
└── REPORT.md

tests/
├── reactor_output.py
├── lb_output.py
└── tasks_output.py
```

The investigation uses tools such as:

- `wrk`
- `perf`
- `strace`
- `pidstat`
- `mpstat`
- Redis `INFO`
- ASan / UBSan

The full benchmark methodology, raw observations, profiling evidence, rejected hypotheses and reproduction commands are documented in:

```text
benchmarks/REPORT.md
```

### Important benchmark note

AORTA has been tested under multiple topologies and CPU-affinity configurations. Numbers from different experiments should **not** be compared as if they came from the same setup.

For example:

- the ~550K RPS result is from the optimized direct-server fast-path comparison
- the ~223K direct result is the direct side of the later controlled LB-vs-backend experiment
- the ~347K result belongs to the separate same-host process-scaling experiment

Before/after claims in this README only compare measurements taken under identical settings.

---

# ✅ Verification

The current performance work was validated with:

- RelWithDebInfo build
- ASan / UBSan build
- HTTP reactor regression tests
- partial-write / `EAGAIN` recovery
- Redis CRUD and native task listing
- LB partial writes
- slow-reader handling
- disconnect handling
- backend distribution
- failover
- health checks
- `git diff --check`

---

# 📊 Prometheus + Grafana

AORTA includes a monitoring stack:

```text
AORTA Services
      │
      ▼
 Prometheus
      │
      ▼
   Grafana
```

The dashboard tracks operational information including throughput, latency percentiles, active connections, backend health and failover-related state.

| Tool | Local address |
|:--|:--|
| AORTA / Task Manager | `http://localhost:9000/` |
| Prometheus | `http://localhost:9090` |
| Grafana | `http://localhost:3000` |
| Raw AORTA metrics | `http://localhost:9000/metrics` |

---

# 🖥️ Platform Requirement

AORTA uses Linux-specific primitives including `epoll`, `accept4` and `SO_REUSEPORT`.

Native execution is intended for Linux.

For Windows, use WSL2 or Docker Desktop. For macOS, Docker Desktop can be used for the complete Compose stack.

---

# 🐳 Run AORTA

## Requirements

```bash
git --version
docker --version
docker compose version
```

## Clone

```bash
git clone https://github.com/Sri-Harshaaa/AORTA.git
cd AORTA
```

## Start the complete stack

```bash
docker compose up -d --build
```

This starts:

```text
⚡ Load Balancer
🖥️ Server 1
🖥️ Server 2
🖥️ Server 3
🔴 Redis
📡 Prometheus
📊 Grafana
```

Verify:

```bash
docker compose ps
```

Health check:

```bash
curl http://localhost:9000/health
```

Task list:

```bash
curl http://localhost:9000/tasks
```

Stop:

```bash
docker compose down
```

---

# 🧱 Native Linux Build

```bash
cmake -S . -B build
cmake --build build -j
```

Generated binaries include:

```text
build/aorta
build/aorta_lb
```

---

# 📁 Project Structure

```text
AORTA/
│
├── apps/
│   ├── lb/
│   └── server/
│
├── benchmarks/
│   ├── REPORT.md
│   ├── run.py
│   ├── profile.py
│   ├── profile_tasks.py
│   ├── scalability.py
│   └── summarize.py
│
├── include/
│   ├── common/
│   ├── http/
│   ├── lb/
│   ├── net/
│   ├── redis/
│   ├── server/
│   ├── task/
│   └── worker/
│
├── src/
│   ├── http/
│   ├── lb/
│   ├── net/
│   ├── redis/
│   ├── server/
│   ├── task/
│   └── worker/
│
├── tests/
│   ├── reactor_output.py
│   ├── lb_output.py
│   └── tasks_output.py
│
├── config/
├── prometheus/
├── grafana/
├── public/
│
├── Dockerfile.lb
├── Dockerfile.server
├── docker-compose.yml
├── CMakeLists.txt
├── LICENSE
└── README.md
```

---

# 📚 Performance Report

For the detailed benchmark methodology, profiler evidence, raw observations and reproduction commands:

```text
benchmarks/REPORT.md
```

---

<div align="center">

## 🫀 AORTA

**Networking · Concurrency · Distributed Systems · Performance Engineering**

*Built to understand what happens underneath.*

</div>

---

## 📜 License

AORTA is released under the MIT License.

See [`LICENSE`](LICENSE) for the full license text.
