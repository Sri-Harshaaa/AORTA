<div align="center">

# ⚡ AORTA

### A miniature distributed system built from the socket up.

**Custom TCP Load Balancer · Event-Driven Servers · Worker Pool · Redis · Prometheus · Grafana**

<br>

[![C++17](https://img.shields.io/badge/C%2B%2B-17-2563EB?style=for-the-badge&logo=cplusplus&logoColor=white)](#)
[![Linux](https://img.shields.io/badge/Linux-epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#)
[![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=for-the-badge&logo=docker&logoColor=white)](#)
[![Redis](https://img.shields.io/badge/Redis-7-DC382D?style=for-the-badge&logo=redis&logoColor=white)](#)
[![Prometheus](https://img.shields.io/badge/Prometheus-E6522C?style=for-the-badge&logo=prometheus&logoColor=white)](#)
[![Grafana](https://img.shields.io/badge/Grafana-F46800?style=for-the-badge&logo=grafana&logoColor=white)](#)

</div>

---

## 🎯 What is AORTA?

AORTA is a **systems-focused miniature distributed backend** built to explore the engineering behind modern infrastructure.

It combines a custom **Layer-4 TCP load balancer**, multiple **event-driven HTTP servers**, a **multi-reactor + worker-pool architecture**, **Redis-backed persistence**, and a **Prometheus + Grafana observability stack**.

The idea is simple:

> **Don't just use infrastructure. Build a small version of the interesting parts and understand what happens underneath.**

```text
                         🌐 Client
                            │
                            ▼
                 ┌─────────────────────┐
                 │   ⚡ AORTA LB        │
                 │                     │
                 │ Layer-4 TCP Proxy   │
                 │ epoll / non-blocking│
                 │ routing / failover  │
                 └──────────┬──────────┘
                            │
                 ┌──────────┼──────────┐
                 │          │          │
                 ▼          ▼          ▼
              🖥️ S1      🖥️ S2      🖥️ S3
                 │          │          │
                 └──────────┼──────────┘
                            │
                            ▼
                       🔴 Redis

              ┌─────────────────────────┐
              │       Observability     │
              │                         │
              │ AORTA → Prometheus      │
              │        → Grafana        │
              └─────────────────────────┘
```

---

## ✨ Highlights

| | |
|:--|:--|
| 🌐 **Layer-4 TCP Proxy** | Custom load balancer using non-blocking sockets and `epoll` |
| ⚡ **Event-Driven I/O** | Reactor-based server architecture |
| 🧵 **Concurrency** | Multi-reactor execution with a worker pool |
| 🔄 **Routing** | Round Robin + Consistent Hashing |
| ❤️ **Reliability** | Backend health checks + failover |
| 🔴 **Persistence** | Redis-backed task management |
| 📊 **Observability** | Prometheus metrics + Grafana dashboard |
| 🐳 **Deployment** | Full Docker Compose environment |
| 🧪 **Benchmarking** | `wrk` based load and latency testing |

---

## 🏗️ Architecture

### Request path

```text
                   🌐 CLIENT
                       │
                       ▼
              ┌─────────────────┐
              │  ⚡ AORTA LB     │
              │      :9000      │
              └────────┬────────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
          ▼            ▼            ▼
     ┌─────────┐  ┌─────────┐  ┌─────────┐
     │ Server 1│  │ Server 2│  │ Server 3│
     │  :8081  │  │  :8082  │  │  :8083  │
     └────┬────┘  └────┬────┘  └────┬────┘
          │            │            │
          └────────────┼────────────┘
                       │
                       ▼
                  🔴 Redis
```

### Backend execution model

```text
                     ⚡ Reactor
                         │
                         │ request needs blocking work
                         ▼
                  ┌─────────────┐
                  │ Worker Queue│
                  └──────┬──────┘
                         │
              ┌──────────┼──────────┐
              ▼          ▼          ▼
           Worker     Worker     Worker
              │          │          │
              └──────────┼──────────┘
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
                      Client
```

---

## ⚡ Core Components

### 1. 🌐 Layer-4 TCP Load Balancer

The load balancer sits between the client and the backend server pool.

It handles:

- client connection acceptance
- backend connection establishment
- TCP data forwarding
- backend selection
- backend health tracking
- failover
- non-blocking event-driven I/O

#### 🔄 Routing

**Round Robin**

```text
Connection 1  → Server 1
Connection 2  → Server 2
Connection 3  → Server 3
Connection 4  → Server 1
...
```

**Consistent Hashing**

Connections can be mapped deterministically while reducing remapping when backend membership changes.

---

### 2. ⚙️ Event-Driven HTTP Server

Each backend server is built around:

- non-blocking sockets
- Linux `epoll`
- reactor-based event handling
- HTTP request parsing
- HTTP response generation
- per-connection state

Rather than assigning one thread to every connection, the reactor monitors many sockets and processes them when I/O becomes ready.

---

### 3. 🧵 Multi-Reactor + Worker Pool

The network event loop should remain responsive.

Redis operations can block, so AORTA moves that work into a worker pool and sends completion notifications back to the reactor.

```text
              Network I/O
                   │
                   ▼
            ⚡ Reactor
                   │
                   ▼
             Worker Queue
                   │
        ┌──────────┼──────────┐
        ▼          ▼          ▼
      Worker     Worker     Worker
        │          │          │
        └──────────┼──────────┘
                   ▼
                🔴 Redis
                   │
                   ▼
                eventfd
                   │
                   ▼
              ⚡ Reactor
```

Each worker maintains its own Redis client connection.

---

### 4. 🔴 Redis Task Manager

AORTA includes a Redis-backed CRUD API.

| Method | Endpoint | Purpose |
|:---:|:---|:---|
| `GET` | `/tasks` | Retrieve all tasks |
| `POST` | `/tasks` | Create a task |
| `PUT` | `/tasks/:id` | Update a task |
| `DELETE` | `/tasks/:id` | Delete a task |

#### 🚀 Retrieval optimization

The original task retrieval path used an **N+1 Redis access pattern**.

It was changed to a **single Lua `EVAL` operation**, reducing unnecessary Redis round trips.

```text
Before                      After

SMEMBERS tasks               Single Lua EVAL
      │                            │
      ├─ HGETALL task:1            ├─ task:1
      ├─ HGETALL task:2            ├─ task:2
      ├─ HGETALL task:3            ├─ task:3
      ├─ ...                       └─ ...
      └─ HGETALL task:N
```

---

### 5. ❤️ Health Checks & Failover

The load balancer tracks backend availability.

```text
        Healthy Backend
              │
              ▼
        Normal Routing
              │
           failure
              │
              ▼
        Health Detection
              │
              ▼
       Mark Unhealthy
              │
              ▼
     Remove From Rotation
              │
              ▼
      Healthy Backends
```

---

## 📊 Observability

AORTA includes a real **Prometheus + Grafana** monitoring stack.

### Metrics exposed

- request counters
- response counters
- active connections
- backend health
- backend failovers
- average latency
- p50 / p95 / p99 latency
- latency histograms
- error counters
- backend connection statistics

### Local monitoring services

> These addresses are **local runtime endpoints**. They become available on the machine running AORTA after the Docker stack has been started. They are not public project links.

| Service | Local address |
|:--|:--|
| ⚡ Load Balancer | `http://localhost:9000` |
| 📊 Metrics | `http://localhost:9000/metrics` |
| 🔎 Prometheus | `http://localhost:9090` |
| 📈 Grafana | `http://localhost:3000` |

---

## 🐳 Getting Started

### ✅ Prerequisites

AORTA is intended to be run with **Docker Compose**.

Install:

- **Git**
- **Docker Engine + Docker Compose plugin**
- or **Docker Desktop** on Windows/macOS

Verify:

```bash
git --version
docker --version
docker compose version
```

Make sure Docker is running before continuing.

---

### 1️⃣ Clone the repository

Replace `<REPOSITORY_URL>` with the GitHub URL of this repository.

```bash
git clone <REPOSITORY_URL>
cd AORTA
```

---

### 2️⃣ Build and start AORTA

The easiest way to launch the complete environment is:

```bash
docker compose up -d --build
```

This builds the AORTA images and starts:

```text
⚡ Load Balancer
🖥️ Server 1
🖥️ Server 2
🖥️ Server 3
🔴 Redis
🔎 Prometheus
📈 Grafana
```

---

### 3️⃣ Verify the deployment

```bash
docker compose ps
```

To inspect logs:

```bash
docker compose logs
```

Or a specific service:

```bash
docker compose logs lb
docker compose logs server1
docker compose logs server2
docker compose logs server3
```

---

## 🧪 Test the System

### ❤️ Health check

After the containers are running:

```bash
curl http://localhost:9000/health
```

Expected:

```text
OK
```

### 📋 Read tasks

```bash
curl http://localhost:9000/tasks
```

### ➕ Create a task

```bash
curl -X POST http://localhost:9000/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA","description":"Finish the distributed system"}'
```

### ✏️ Update a task

```bash
curl -X PUT http://localhost:9000/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA v2","description":"Improve performance"}'
```

### 🗑️ Delete a task

```bash
curl -X DELETE http://localhost:9000/tasks/1
```

---

## 📈 Performance

AORTA was benchmarked with [`wrk`](https://github.com/wg/wrk) under increasing client concurrency.

Example:

```bash
wrk --latency -t4 -c500 -d30s http://localhost:9000/tasks
```

> ⚠️ These figures are measurements from the tested build. Actual results depend on hardware, operating system, Docker configuration, workload, and system state.

### ❤️ `/health`

| Concurrency | Throughput | Avg | p50 | p99 |
|---:|---:|---:|---:|---:|
| 100 | **100.9K RPS** | 0.96 ms | 0.85 ms | 2.80 ms |
| 500 | **100.3K RPS** | 4.98 ms | 4.33 ms | 15.99 ms |
| 1000 | **73.2K RPS** | 12.96 ms | 11.86 ms | 34.60 ms |
| 2000 | **89.1K RPS** | 22.08 ms | 19.98 ms | 60.35 ms |
| 5000 | **60.9K RPS** | 80.63 ms | 74.94 ms | 170.28 ms |

### 📋 `/tasks`

| Concurrency | Throughput | Avg | p50 | p99 |
|---:|---:|---:|---:|---:|
| 100 | **23.9K RPS** | 4.19 ms | 3.84 ms | 8.75 ms |
| 500 | **20.8K RPS** | 24.00 ms | 22.12 ms | 43.50 ms |
| 1000 | **19.4K RPS** | 51.34 ms | 46.77 ms | 88.38 ms |
| 2000 | **18.6K RPS** | 107.03 ms | 102.07 ms | 166.52 ms |
| 5000 | **20.2K RPS** | 244.61 ms | 222.51 ms | 402.58 ms |

### 📄 Full benchmark report

See:

```text
docs/AORTA_Benchmark_Performance_Report.pdf
```

---

## 🔬 Performance Engineering

The project was optimized through measurement rather than speculative tuning.

```text
📏 Measure
    ↓
🔎 Identify bottleneck
    ↓
🛠️ Make one targeted change
    ↓
🧪 Benchmark again
    ↓
✅ Keep   /   ↩️ Revert
```

| Area | Optimization |
|:--|:--|
| 🔴 Redis | Replaced N+1 task retrieval with one Lua operation |
| ⚡ Networking | Non-blocking sockets + `epoll` |
| 🧵 Concurrency | Redis work moved to worker threads |
| 📡 Completion | `eventfd` used for worker-to-reactor notification |
| 🧹 Epoll | Avoided unnecessary `epoll_ctl` modifications |

---

## 📁 Project Structure

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

## 🧱 Native Linux Build

Docker Compose is the recommended way to run the complete distributed stack.

For native Linux development:

```bash
cmake -S . -B build
cmake --build build -j
```

The build output is generated under:

```text
build/
```

---

## 🛑 Stop AORTA

Stop the containers:

```bash
docker compose down
```

Stop the containers and remove persistent volumes:

```bash
docker compose down -v
```

> Removing volumes also removes persistent Redis and Grafana data created by the Compose deployment.

---

## ✅ Current Status

```text
✅ Custom Layer-4 TCP Load Balancer
✅ Linux epoll + non-blocking sockets
✅ Multi-reactor architecture
✅ Worker pool
✅ Redis persistence
✅ Round Robin routing
✅ Consistent Hashing
✅ Backend health checks
✅ Backend failover
✅ HTTP CRUD task API
✅ Prometheus metrics
✅ Grafana dashboard
✅ Docker Compose deployment
✅ Performance benchmarking
✅ Benchmark report
```

---

## 🎓 What AORTA Explores

```text
        🌐 Networking
              │
              ▼
          TCP / Sockets
              │
              ▼
            epoll
              │
              ▼
      🧵 Concurrency
              │
              ▼
        🔴 Persistence
              │
              ▼
        ❤️ Reliability
              │
              ▼
       📊 Observability
              │
              ▼
        🧪 Performance
```

AORTA is ultimately an exploration of the layers that sit between an application and the operating system:

**HTTP → TCP → Sockets → epoll → Threads → Kernel**

---

## 📚 Documentation

Detailed benchmark results and analysis:

**`docs/AORTA_Benchmark_Performance_Report.pdf`**

---

<div align="center">

# ⚡ AORTA

### Networking · Concurrency · Distributed Systems · Performance

*Built to understand what happens underneath.*

</div>

---

## 📜 License

Add the project's license here.
