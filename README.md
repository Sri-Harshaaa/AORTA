<div align="center">

# ⚡ AORTA

### **A Miniature Distributed System Built From Scratch**

*A systems-focused project exploring networking, concurrency, load balancing, persistence, observability, and performance engineering — without hiding the interesting parts behind existing infrastructure.*

<br>

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Redis](https://img.shields.io/badge/Redis-7-DC382D?style=for-the-badge&logo=redis&logoColor=white)
![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=for-the-badge&logo=docker&logoColor=white)
![Prometheus](https://img.shields.io/badge/Prometheus-Monitoring-E6522C?style=for-the-badge&logo=prometheus&logoColor=white)
![Grafana](https://img.shields.io/badge/Grafana-Dashboard-F46800?style=for-the-badge&logo=grafana&logoColor=white)

<br>

**Custom TCP Load Balancer • Event-Driven HTTP Servers • Worker Pool • Redis • Prometheus • Grafana**

</div>

---

## 🧠 What is AORTA?

AORTA is a miniature distributed backend system built to understand what happens **underneath** modern backend infrastructure.

Instead of simply deploying Nginx, HAProxy, Redis, and monitoring tools, the project implements the important networking and concurrency pieces directly:

```text
                🌐 Client
                   │
                   ▼
        ┌──────────────────────┐
        │  ⚡ AORTA LoadBalancer │
        │                      │
        │  Layer-4 TCP Proxy   │
        │  epoll               │
        │  Health Checks       │
        │  Failover            │
        │  Routing             │
        └──────────┬───────────┘
                   │
          ┌────────┼────────┐
          │        │        │
          ▼        ▼        ▼
       🖥️ S1    🖥️ S2    🖥️ S3
          │        │        │
          └────────┼────────┘
                   │
                   ▼
               🔴 Redis
                   │
                   ▼
          📊 Prometheus
                   │
                   ▼
             📈 Grafana
```

The project is intentionally designed as a **systems engineering exercise** rather than just another CRUD application.

---

# ✨ Features

| Area | What AORTA provides |
|------|----------------------|
| 🌐 Networking | Non-blocking TCP sockets with Linux `epoll` |
| ⚡ Load Balancing | Custom Layer-4 TCP proxy |
| 🔄 Routing | Round Robin + Consistent Hashing |
| ❤️ Reliability | Backend health checking + failover |
| 🧵 Concurrency | Multi-reactor architecture + worker pool |
| 🚀 Async Work | Redis operations moved away from reactor threads |
| 🗄️ Persistence | Redis-backed task management |
| 📡 Observability | Prometheus-compatible application metrics |
| 📊 Visualization | Grafana monitoring dashboard |
| 🐳 Deployment | Fully containerized with Docker Compose |
| 🧪 Performance | `wrk` based concurrency and latency benchmarks |
| 🛠️ API | CRUD task management over HTTP |

---

# 🏗️ Architecture

### High-level system

```text
                              🌐 CLIENT
                         curl / wrk / browser
                                  │
                                  ▼
                   ┌────────────────────────────┐
                   │      ⚡ AORTA LB :9000      │
                   │                            │
                   │   Layer-4 TCP Proxy        │
                   │   Linux epoll              │
                   │   Round Robin               │
                   │   Consistent Hashing       │
                   │   Health Checks             │
                   │   Failover                  │
                   └─────────────┬──────────────┘
                                 │
                  ┌──────────────┼──────────────┐
                  │              │              │
                  ▼              ▼              ▼
           ┌────────────┐ ┌────────────┐ ┌────────────┐
           │  🖥️ Server1 │ │  🖥️ Server2 │ │  🖥️ Server3 │
           │    :8081   │ │    :8082   │ │    :8083   │
           │            │ │            │ │            │
           │   epoll    │ │   epoll    │ │   epoll    │
           │ HTTP Server│ │ HTTP Server│ │ HTTP Server│
           │ WorkerPool │ │ WorkerPool │ │ WorkerPool │
           └──────┬─────┘ └──────┬─────┘ └──────┬─────┘
                  │              │              │
                  └──────────────┼──────────────┘
                                 │
                                 ▼
                         ┌─────────────────┐
                         │   🔴 Redis 7    │
                         │                 │
                         │ Task Persistence│
                         └─────────────────┘


        ┌───────────────────────────────────────────────┐
        │               📊 OBSERVABILITY                 │
        │                                                │
        │  AORTA Metrics ──► Prometheus ──► Grafana    │
        └───────────────────────────────────────────────┘
```

---

# ⚡ Core Components

## 🌐 1. Layer-4 TCP Load Balancer

A custom TCP load balancer sits between clients and backend servers.

It handles:

- accepting client connections
- establishing backend connections
- forwarding TCP data
- maintaining backend state
- detecting unhealthy backends
- routing traffic only to healthy servers

### 🔄 Routing algorithms

**Round Robin**

```text
Client 1 → Server 1
Client 2 → Server 2
Client 3 → Server 3
Client 4 → Server 1
...
```

**Consistent Hashing**

Connections are deterministically mapped to backend nodes while minimizing remapping when backend membership changes.

---

## ⚙️ 2. Event-Driven HTTP Servers

Each backend server uses Linux `epoll` with non-blocking sockets.

Instead of creating a dedicated thread for every client connection:

```text
          Many Connections
                 │
                 ▼
        ┌─────────────────┐
        │  epoll Reactor  │
        └────────┬────────┘
                 │
         Ready I/O Events
                 │
                 ▼
        Request Processing
```

This allows the server to handle large numbers of concurrent connections without a one-thread-per-connection architecture.

---

## 🧵 3. Multi-Reactor + Worker Pool

Blocking operations such as Redis access are separated from the network event loop.

```text
                 ⚡ Reactor
                     │
                     │ async job
                     ▼
              ┌──────────────┐
              │ Worker Queue │
              └──────┬───────┘
                     │
          ┌──────────┼──────────┐
          ▼          ▼          ▼
       Worker     Worker     Worker
          │          │          │
          └──────────┼──────────┘
                     │
                     ▼
                  🔴 Redis
                     │
                     ▼
              Completion Event
                     │
                     ▼
                 ⚡ Reactor
                     │
                     ▼
                  Client
```

Each worker maintains a persistent Redis connection, while completion notifications are delivered back to the reactor through `eventfd`.

---

# 🗄️ Redis Task Management

AORTA exposes a small task-management API backed by Redis.

### Supported endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/tasks` | Get all tasks |
| `POST` | `/tasks` | Create a task |
| `PUT` | `/tasks/:id` | Update a task |
| `DELETE` | `/tasks/:id` | Delete a task |

### 🚀 Redis optimization

The original task retrieval path performed an **N+1 Redis access pattern**.

That path was optimized to retrieve the task collection through a **single Lua `EVAL` operation**, reducing unnecessary Redis round trips.

---

# ❤️ Reliability & Failover

AORTA actively tracks backend health.

```text
           Backend Healthy
                 │
                 ▼
           Normal Routing
                 │
                 │ failure
                 ▼
          ❤️ Health Check
                 │
                 ▼
        Backend Marked Unhealthy
                 │
                 ▼
       Removed From Rotation
                 │
                 ▼
     Traffic → Healthy Backends
```

This allows the load balancer to continue serving traffic when an individual backend becomes unavailable.

---

# 📊 Observability

AORTA comes with a real monitoring stack rather than a custom fake dashboard.

```text
┌─────────────┐
│ AORTA       │
│ Services    │
└──────┬──────┘
       │ metrics
       ▼
┌─────────────┐
│ Prometheus  │
└──────┬──────┘
       │ queries
       ▼
┌─────────────┐
│   Grafana   │
└─────────────┘
```

### Metrics include

- 📈 request / response counters
- 🔌 active connections
- ❤️ backend health
- 🔁 backend failovers
- ⏱️ average latency
- 📊 p50 / p95 / p99 latency
- 📉 latency histograms
- ❌ error counts
- 🔗 backend connection statistics

### Monitoring URLs

| Service | URL |
|---------|-----|
| ⚡ AORTA Load Balancer | `http://127.0.0.1:9000` |
| 📊 Metrics | `http://127.0.0.1:9000/metrics` |
| 🔎 Prometheus | `http://127.0.0.1:9090` |
| 📈 Grafana | `http://127.0.0.1:3000` |

---

# 🐳 Docker Deployment

The complete distributed stack is available through Docker Compose.

### Containers

```text
🔴 Redis
🖥️ Server 1
🖥️ Server 2
🖥️ Server 3
⚡ Load Balancer
🔎 Prometheus
📈 Grafana
```

All services run inside a dedicated Docker bridge network.

---

# 🚀 Quick Start

## 1️⃣ Requirements

Install:

- Docker
- Docker Compose
- Git

For native Linux development:

- CMake
- C++ compiler
- Linux with `epoll` support
- Redis

---

## 2️⃣ Clone

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd AORTA
```

---

## 3️⃣ Start everything

```bash
docker compose up -d --build
```

Check containers:

```bash
docker compose ps
```

---

# 🧪 Test the System

## ❤️ Health Check

```bash
curl http://127.0.0.1:9000/health
```

Expected:

```text
OK
```

---

## 📋 Get Tasks

```bash
curl http://127.0.0.1:9000/tasks
```

---

## ➕ Create a Task

```bash
curl -X POST http://127.0.0.1:9000/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA","description":"Finish the distributed system"}'
```

---

## ✏️ Update a Task

```bash
curl -X PUT http://127.0.0.1:9000/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA v2","description":"Improve performance"}'
```

---

## 🗑️ Delete a Task

```bash
curl -X DELETE http://127.0.0.1:9000/tasks/1
```

---

# 📈 Performance

AORTA was benchmarked using [`wrk`](https://github.com/wg/wrk) under increasing client concurrency.

Example:

```bash
wrk --latency -t4 -c500 -d30s http://127.0.0.1:9000/tasks
```

## ❤️ `/health`

| Concurrency | Throughput | Avg | p50 | p99 |
|------------:|-----------:|----:|----:|----:|
| 100  | **100.9K RPS** | 0.96 ms | 0.85 ms | 2.80 ms |
| 500  | **100.3K RPS** | 4.98 ms | 4.33 ms | 15.99 ms |
| 1000 | **73.2K RPS**  | 12.96 ms | 11.86 ms | 34.60 ms |
| 2000 | **89.1K RPS**  | 22.08 ms | 19.98 ms | 60.35 ms |
| 5000 | **60.9K RPS**  | 80.63 ms | 74.94 ms | 170.28 ms |

## 📋 `/tasks`

| Concurrency | Throughput | Avg | p50 | p99 |
|------------:|-----------:|----:|----:|----:|
| 100  | **23.9K RPS** | 4.19 ms | 3.84 ms | 8.75 ms |
| 500  | **20.8K RPS** | 24.00 ms | 22.12 ms | 43.50 ms |
| 1000 | **19.4K RPS** | 51.34 ms | 46.77 ms | 88.38 ms |
| 2000 | **18.6K RPS** | 107.03 ms | 102.07 ms | 166.52 ms |
| 5000 | **20.2K RPS** | 244.61 ms | 222.51 ms | 402.58 ms |

> ⚠️ Benchmark results depend on CPU, Docker configuration, workload, and system state. These are measurements from the tested build, not hardware-independent guarantees.

📄 Detailed results and analysis:

```text
docs/AORTA_Benchmark_Performance_Report.pdf
```

---

# 🔬 Performance Engineering

AORTA was optimized through **measurement-driven iteration** rather than random micro-optimizations.

### 🗄️ Redis

Replaced an N+1 Redis retrieval path with a single Lua operation.

### ⚡ Networking

Used non-blocking sockets and `epoll` for event-driven connection handling.

### 🧵 Concurrency

Moved blocking Redis operations into a worker pool.

### 📡 Completion Events

Used `eventfd` to notify reactors when worker jobs complete.

### 🧹 Epoll Efficiency

Cached epoll state where possible and avoided unnecessary `epoll_ctl` modifications.

### 🔁 Development loop

```text
       📏 Measure
           │
           ▼
      🔎 Find Bottleneck
           │
           ▼
      🛠️ One Change
           │
           ▼
      🧪 Benchmark
           │
       ┌───┴───┐
       ▼       ▼
     Keep    Revert
```

---

# 📁 Project Structure

```text
AORTA/
│
├── CMakeLists.txt
├── Dockerfile.lb
├── Dockerfile.server
├── docker-compose.yml
├── .gitignore
├── README.md
│
├── config/
│   └── backends.docker.conf
│
├── include/
│   ├── common/
│   ├── http/
│   ├── lb/
│   └── server/
│
├── src/
│   ├── http/
│   ├── lb/
│   └── server/
│
├── prometheus/
│   └── prometheus.yml
│
├── grafana/
│   ├── dashboards/
│   └── provisioning/
│
└── docs/
    └── AORTA_Benchmark_Performance_Report.pdf
```

---

# 🧱 Native Build

For Linux development without Docker:

```bash
cmake -S . -B build
cmake --build build -j
```

Generated binaries:

```text
build/
```

---

# 🧩 Design Philosophy

AORTA is mainly a **systems + backend engineering project**.

The project tries to make the layers visible:

```text
┌──────────────────────┐
│      Application     │
├──────────────────────┤
│         HTTP         │
├──────────────────────┤
│         TCP          │
├──────────────────────┤
│       Sockets        │
├──────────────────────┤
│        epoll         │
├──────────────────────┤
│ Threads / Scheduling │
├──────────────────────┤
│       Kernel         │
└──────────────────────┘
```

Instead of treating infrastructure as a black box, AORTA focuses on understanding and implementing the mechanisms underneath it.

---

# 🛑 Stop the Stack

Stop containers:

```bash
docker compose down
```

Remove containers **and persistent volumes**:

```bash
docker compose down -v
```

---

# 📚 Documentation

Detailed benchmark report:

```text
docs/AORTA_Benchmark_Performance_Report.pdf
```

---

# ✅ Current Status

AORTA currently includes:

- ✅ Custom Layer-4 TCP Load Balancer
- ✅ Linux `epoll` networking
- ✅ Non-blocking sockets
- ✅ Multi-reactor architecture
- ✅ Worker pool
- ✅ Redis persistence
- ✅ Round Robin routing
- ✅ Consistent Hashing
- ✅ Backend health checks
- ✅ Backend failover
- ✅ Prometheus metrics
- ✅ Grafana dashboard
- ✅ Docker Compose deployment
- ✅ CRUD Task API
- ✅ Performance benchmarking
- ✅ Benchmark documentation

---

<div align="center">

### ⚡ Built to understand the system, not just use it.

**AORTA — Networking • Concurrency • Distributed Systems • Performance**

</div>

---

## 📜 License

Add the project's license here.
