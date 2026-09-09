<div align="center">

# ⚡ AORTA

### A high-concurrency distributed server platform built around a multi-reactor HTTP server and a custom Layer-4 TCP load balancer.

<br>

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](#)
[![Linux](https://img.shields.io/badge/Linux-epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#)
[![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=for-the-badge&logo=docker&logoColor=white)](#)
[![Redis](https://img.shields.io/badge/Redis-7-DC382D?style=for-the-badge&logo=redis&logoColor=white)](#)
[![Prometheus](https://img.shields.io/badge/Prometheus-E6522C?style=for-the-badge&logo=prometheus&logoColor=white)](#)
[![Grafana](https://img.shields.io/badge/Grafana-F46800?style=for-the-badge&logo=grafana&logoColor=white)](#)

</div>

---

## 🎯 What AORTA Is

AORTA is a **high-concurrency distributed server system** written in modern C++ and built to expose the mechanisms normally hidden behind production infrastructure.

The system combines a **custom Layer-4 TCP load balancer**, a pool of **multi-reactor event-driven HTTP servers**, a **custom HTTP/1.1 parsing and routing layer**, asynchronous **worker-pool execution**, **Redis-backed task persistence**, and a **Prometheus + Grafana observability stack**.

The architecture is designed around one central separation:

> **Network I/O stays event-driven; blocking application work is moved out of the reactor.**

That makes AORTA a systems project first — the Task Manager application is the workload used to exercise the infrastructure.

---

## 🏗️ System Architecture

```text
                                  🌐 Client
                                     │
                                     ▼
                     ┌────────────────────────────┐
                     │     ⚡ AORTA Load Balancer  │
                     │                            │
                     │   Layer-4 TCP Proxy        │
                     │   Multi-Reactor + epoll    │
                     │   Round Robin / Hashing    │
                     │   Health Checks / Failover │
                     └──────────────┬─────────────┘
                                    │
                   ┌────────────────┼────────────────┐
                   │                │                │
                   ▼                ▼                ▼
              ┌──────────┐     ┌──────────┐     ┌──────────┐
              │ Server 1 │     │ Server 2 │     │ Server 3 │
              │   8081   │     │   8082   │     │   8083   │
              │          │     │          │     │          │
              │ Reactor  │     │ Reactor  │     │ Reactor  │
              │ HTTP/1.1 │     │ HTTP/1.1 │     │ HTTP/1.1 │
              │ WorkerPool│     │ WorkerPool│     │ WorkerPool│
              └────┬─────┘     └────┬─────┘     └────┬─────┘
                   │                │                │
                   └────────────────┼────────────────┘
                                    │
                                    ▼
                              🔴 Redis 7
                           Task persistence


                   ┌─────────────────────────┐
                   │     📊 Observability    │
                   │                         │
                   │ AORTA → Prometheus      │
                   │        → Grafana        │
                   └─────────────────────────┘
```

### Multi-reactor execution

The servers are organized around reactor instances. A reactor owns its connection state and `epoll` loop, while worker threads handle blocking Redis work. The worker completion path returns to the reactor through `eventfd`. The load-balancer implementation also carries a reactor ID/count and runs its own `epoll` event loop with health-timer handling. fileciteturn76file1L79-L91 fileciteturn76file4L384-L409

---

## 🌐 Networking & HTTP Engine

AORTA does more than proxy HTTP traffic. The backend servers contain their own lightweight HTTP processing stack.

```text
TCP bytes
    │
    ▼
┌──────────────────────┐
│   Custom HTTP Parser │
│                      │
│ Request Line         │
│ Headers              │
│ Body                 │
│ Chunked Encoding     │
│ Trailers             │
└──────────┬───────────┘
           │
           ▼
      HttpRequest
           │
           ▼
       HTTP Router
           │
           ▼
     Route Handler
           │
           ▼
     HttpResponse
```

### HTTP parser

The parser is incremental and stateful. It tracks:

- request line
- headers
- request body
- chunk-size parsing
- chunk data
- terminating CRLF
- trailer headers

It also rejects malformed or oversized input using explicit limits:

| Limit | Value |
|:--|--:|
| Request line | 8 KiB |
| Individual header line | 8 KiB |
| Total headers | 32 KiB |
| Header count | 100 |
| Request body | 1 MiB |

The parser exposes `NeedMoreData`, `Complete`, malformed-request, size-limit, transfer-encoding, and version-related parse outcomes. fileciteturn75file0L21-L51

### Supported HTTP methods

| Method | Support |
|:---:|:---|
| `GET` | ✅ |
| `POST` | ✅ |
| `PUT` | ✅ |
| `DELETE` | ✅ |
| `HEAD` | ✅ |
| `OPTIONS` | ✅ |

The handler layer routes `GET`, `POST`, `PUT`, and `DELETE` requests to the task API, while `HEAD` and `OPTIONS` are handled at the HTTP layer. fileciteturn74file2L343-L421

The `OPTIONS` response advertises:

```text
GET, HEAD, POST, PUT, DELETE, OPTIONS
```

and `HEAD` is handled without sending a response body. fileciteturn78file1L179-L235

### Static content

The server can also serve the frontend from its `public` directory. Requests for `/` and file-like paths are passed through the static-file handler. fileciteturn78file8L1072-L1085

---

## ⚡ High-Concurrency Server Model

AORTA is designed around **event-driven, non-blocking I/O** instead of a thread-per-connection model.

Each accepted connection is made non-blocking and registered with `epoll`. Connection state is tracked by the reactor, including deadlines and request-processing state. fileciteturn76file1L95-L153

```text
             Many TCP Connections
                       │
                       ▼
                ┌─────────────┐
                │ epoll Reactor│
                └──────┬──────┘
                       │
                Ready I/O Events
                       │
              ┌────────┴────────┐
              │                 │
              ▼                 ▼
        Fast local work     Blocking work
                                │
                                ▼
                         🧵 Worker Pool
                                │
                                ▼
                             🔴 Redis
                                │
                                ▼
                             eventfd
                                │
                                ▼
                         epoll Reactor
```

The point is not merely "using threads". The architecture makes the reactor responsible for I/O while workers absorb operations that could otherwise stall it.

---

## 🌍 Layer-4 Load Balancer

The AORTA load balancer is a **TCP-level proxy**. It accepts client connections, selects a backend, and forwards traffic without requiring HTTP semantics at the proxy layer.

This is an important architectural boundary:

```text
                 L4
Client ─────────► Load Balancer ─────────► Backend
                    TCP proxy
                                             │
                                             ▼
                                          HTTP/1.1
                                          Parser
                                             │
                                             ▼
                                          Router
```

### Routing modes

**Round Robin**

```text
Connection 1 → Server 1
Connection 2 → Server 2
Connection 3 → Server 3
Connection 4 → Server 1
...
```

**Consistent Hashing**

AORTA can build a consistent-hash ring with virtual nodes and route connections deterministically. The active routing mode can be selected through `AORTA_LB_ROUTING`; `round_robin` is the default and hash-based values select consistent hashing. fileciteturn76file4L363-L399

### Health checks and failover

The load balancer maintains backend health state and runs health checks from its event loop using a timer integrated with `epoll`. fileciteturn79file3L357-L375

When a backend becomes unavailable:

```text
Healthy
   │
   │ failure
   ▼
Health Check
   │
   ▼
Mark Unhealthy
   │
   ▼
Remove From Rotation
   │
   ▼
Route To Healthy Servers
```

---

## 📝 Task Manager — The Demo Application

AORTA includes a complete **Task Manager web application** to exercise the distributed backend.

The browser UI is served by the AORTA HTTP server itself. The frontend communicates with the `/tasks` API and supports creating, editing, completing, and deleting tasks. fileciteturn75file5L541-L637

### What the application demonstrates

| Feature | Description |
|:--|:--|
| ➕ Create | Add a new task |
| ✅ Complete | Toggle task completion |
| ✏️ Edit | Modify an existing task |
| 🗑️ Delete | Remove a task |
| 💾 Persistence | Tasks are stored in Redis |
| 🔁 Distributed path | Requests go through the AORTA load balancer |

### Open the application

After the Docker stack is running, open:

**`http://localhost:9000/`**

That is the **Task Manager web UI**.

The `/tasks` endpoint remains available as the JSON API:

```text
GET     /tasks
POST    /tasks
PUT     /tasks/:id
DELETE  /tasks/:id
```

The backend also exposes:

```text
GET /hello
GET /health
GET /metrics
```

The current handler implementation returns `OK` for `/health`, exposes Prometheus-formatted metrics at `/metrics`, and provides the task CRUD handlers. fileciteturn74file1L170-L227 fileciteturn78file5L536-L567

---

## 📊 Observability Dashboard

AORTA ships with a **real Grafana dashboard backed by Prometheus**.

It is not a mock frontend and it does not generate fake monitoring data.

```text
AORTA Services
      │
      ▼
 Prometheus
      │
      ▼
  Grafana
```

The repository contains the Grafana dashboard JSON and provisioning configuration, so the monitoring stack can be brought up together with the rest of the system. fileciteturn76file0L32-L42

### Dashboard view

The dashboard is designed around operational questions such as:

- Are all backends healthy?
- How much traffic is the system handling?
- How are latency percentiles changing?
- How many active connections exist?
- Which backend servers are healthy?
- How many failovers have occurred?

The dashboard includes global throughput, latency percentile panels, per-server health/status cards, and connection metrics. fileciteturn77file4L197-L233

### Open the dashboard

After starting AORTA:

**Grafana:** `http://localhost:3000`

**Prometheus:** `http://localhost:9090`

**Raw AORTA metrics:** `http://localhost:9000/metrics`

> These are **local runtime addresses**. They become available on the machine running the Docker stack; they are not public links to somebody else's instance.

---

## 🐳 Running AORTA

### Requirements

Install:

- **Git**
- **Docker Engine + Docker Compose**
- or **Docker Desktop** on Windows/macOS

Verify:

```bash
git --version
docker --version
docker compose version
```

Make sure Docker is running.

### Start the complete stack

Clone the repository:

```bash
git clone https://github.com/Sri-Harshaaa/Aorta.git
cd Aorta
```

Build and start every service:

```bash
docker compose up -d --build
```

Check the deployment:

```bash
docker compose ps
```

The Compose stack contains:

```text
⚡ Load Balancer
🖥️ Server 1
🖥️ Server 2
🖥️ Server 3
🔴 Redis
📡 Prometheus
📊 Grafana
```

### First things to try

After the services are up:

```bash
curl http://localhost:9000/health
```

Expected:

```text
OK
```

Then open the Task Manager in your browser:

```text
http://localhost:9000/
```

And open the monitoring dashboard:

```text
http://localhost:3000
```

---

## 📈 Performance

AORTA was benchmarked using [`wrk`](https://github.com/wg/wrk) with increasing client concurrency.

Example:

```bash
wrk --latency -t4 -c500 -d30s http://localhost:9000/tasks
```

### `/health`

| Concurrency | Throughput | Avg | p50 | p99 |
|---:|---:|---:|---:|---:|
| 100 | **100.9K RPS** | 0.96 ms | 0.85 ms | 2.80 ms |
| 500 | **100.3K RPS** | 4.98 ms | 4.33 ms | 15.99 ms |
| 1000 | **73.2K RPS** | 12.96 ms | 11.86 ms | 34.60 ms |
| 2000 | **89.1K RPS** | 22.08 ms | 19.98 ms | 60.35 ms |
| 5000 | **60.9K RPS** | 80.63 ms | 74.94 ms | 170.28 ms |

### `/tasks`

| Concurrency | Throughput | Avg | p50 | p99 |
|---:|---:|---:|---:|---:|
| 100 | **23.9K RPS** | 4.19 ms | 3.84 ms | 8.75 ms |
| 500 | **20.8K RPS** | 24.00 ms | 22.12 ms | 43.50 ms |
| 1000 | **19.4K RPS** | 51.34 ms | 46.77 ms | 88.38 ms |
| 2000 | **18.6K RPS** | 107.03 ms | 102.07 ms | 166.52 ms |
| 5000 | **20.2K RPS** | 244.61 ms | 222.51 ms | 402.58 ms |

The measured `/health` path reaches roughly **100K requests/sec** at lower concurrency, while the Redis-backed `/tasks` path reaches roughly **24K requests/sec** in the latest run. The detailed benchmark report also records an optimized `/tasks` peak of about **28.9K requests/sec** from an earlier final-build measurement. fileciteturn76file8L762-L775 fileciteturn76file6L539-L552 fileciteturn73file3L197-L212

📄 Full benchmark report:

`docs/AORTA_Benchmark_Performance_Report.pdf`

---

## 🔬 Performance Engineering

AORTA was tuned using a **measure → identify → change → benchmark** workflow.

Key engineering work includes:

**Redis retrieval optimization**  
The task retrieval path was changed from an N+1 access pattern to a single Lua `EVAL` operation.

**Reactor isolation**  
Blocking Redis operations were moved out of the network event loop and into worker threads.

**Worker completion signaling**  
`eventfd` connects worker completion back to the reactor without busy polling.

**Epoll efficiency**  
The networking path avoids unnecessary `epoll_ctl` modifications where cached state is sufficient.

```text
📏 Measure
   ↓
🔎 Identify bottleneck
   ↓
🛠️ Make one change
   ↓
🧪 Benchmark
   ↓
✅ Keep / ↩️ Revert
```

---

## 🗂️ Project Structure

```text
Aorta/
│
├── apps/
│   ├── lb/
│   └── server/
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
├── config/
│   └── backends.docker.conf
│
├── prometheus/
│   └── prometheus.yml
│
├── grafana/
│   ├── dashboards/
│   └── provisioning/
│
├── docs/
│   └── AORTA_Benchmark_Performance_Report.pdf
│
├── Dockerfile.lb
├── Dockerfile.server
├── docker-compose.yml
├── CMakeLists.txt
└── README.md
```

---

## 🧱 Native Linux Build

Docker Compose is the recommended way to run the full distributed system.

For native development:

```bash
cmake -S . -B build
cmake --build build -j
```

The build produces:

```text
build/aorta
build/aorta_lb
```

Build artifacts are intentionally excluded from version control.

---

## 🛑 Stop the System

Stop the services:

```bash
docker compose down
```

Remove the services **and persistent volumes**:

```bash
docker compose down -v
```

Removing the volumes also removes persisted Redis and Grafana data associated with the Compose deployment.

---

## 📚 Further Documentation

The repository includes a detailed benchmark and performance report:

**`docs/AORTA_Benchmark_Performance_Report.pdf`**

---

<div align="center">

# ⚡ AORTA

### Multi-Reactor · epoll · TCP Load Balancing · HTTP · Redis · Observability

**Built to understand and measure high-concurrency server architecture.**

</div>

---

## 📜 License

Add the project's license here.
