<div align="center">

<h2>⚡ AORTA</h2>

<h4>A Miniature Distributed System Built From Scratch</h4>

<p><em>A systems-focused project exploring networking, concurrency, load balancing, persistence, observability, and performance engineering — without hiding the interesting parts behind existing infrastructure.</em></p>

<br>

[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](#)
[![Linux](https://img.shields.io/badge/Linux-epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black)](#)
[![Redis](https://img.shields.io/badge/Redis-7-DC382D?style=for-the-badge&logo=redis&logoColor=white)](#)
[![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?style=for-the-badge&logo=docker&logoColor=white)](#)
[![Prometheus](https://img.shields.io/badge/Prometheus-Monitoring-E6522C?style=for-the-badge&logo=prometheus&logoColor=white)](#)
[![Grafana](https://img.shields.io/badge/Grafana-Dashboard-F46800?style=for-the-badge&logo=grafana&logoColor=white)](#)

<br>

<sub>Custom TCP Load Balancer · Event-Driven HTTP Servers · Worker Pool · Redis · Prometheus · Grafana</sub>

</div>

---

## 🎯 What is AORTA?

AORTA is a **high-concurrency distributed server system** built to explore how modern backend infrastructure handles networking, concurrency, persistence, failures, observability, and load.

At the center is a **custom Layer-4 TCP load balancer** distributing connections across multiple **multi-reactor, event-driven HTTP servers**. Each server uses Linux `epoll`, non-blocking sockets, a custom HTTP/1.1 parser and router, asynchronous worker execution, and Redis-backed persistence.

The **Task Manager** is the application workload running on top of this infrastructure, giving the system something real to serve, persist, distribute, monitor, and benchmark.

> **AORTA is built to understand the infrastructure underneath a web application — not just the application itself.**

---

## 🏗️ Architecture

```text
                                  🌐 CLIENT
                                      │
                                      ▼
                           ┌──────────────────────┐
                           │    ⚡ AORTA LB        │
                           │                      │
                           │ Layer-4 TCP Proxy    │
                           │ Multi-Reactor / epoll│
                           │ Routing + Health     │
                           │ Failover             │
                           └──────────┬───────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
              ┌────────────┐   ┌────────────┐   ┌────────────┐
              │  SERVER 1  │   │  SERVER 2  │   │  SERVER 3  │
              │   :8081    │   │   :8082    │   │   :8083    │
              │            │   │            │   │            │
              │  Reactor   │   │  Reactor   │   │  Reactor   │
              │  HTTP/1.1  │   │  HTTP/1.1  │   │  HTTP/1.1  │
              │ WorkerPool │   │ WorkerPool │   │ WorkerPool │
              └─────┬──────┘   └─────┬──────┘   └─────┬──────┘
                    │                 │                 │
                    └─────────────────┼─────────────────┘
                                      │
                                      ▼
                               ┌─────────────┐
                               │  🔴 Redis   │
                               │ Task Store  │
                               └─────────────┘
```

### 🔁 Request Flow

```text
TCP connection
      │
      ▼
Layer-4 Load Balancer
      │
      ▼
Backend Reactor
      │
      ▼
HTTP/1.1 Parser
      │
      ▼
HTTP Router
      │
      ▼
Handler / Task Manager
      │
      ├──────────────► local response
      │
      └──────────────► Worker Pool → Redis
                               │
                               ▼
                            eventfd
                               │
                               ▼
                            Reactor
```

---

## ⚡ High-Concurrency Design

AORTA follows an **event-driven multi-reactor pattern** rather than a thread-per-connection model.

```text
               Many TCP Connections
                        │
                        ▼
                 ┌─────────────┐
                 │ epoll Reactor│
                 └──────┬──────┘
                        │
                   Ready I/O
                        │
              ┌─────────┴─────────┐
              │                   │
              ▼                   ▼
        Non-blocking work     Blocking work
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
                              ⚡ Reactor
```

Connections are accepted as non-blocking sockets and registered with `epoll`; the reactor also tracks per-connection state and deadlines. fileciteturn76file1L95-L153

---

## 🌐 HTTP/1.1 Engine

AORTA contains its own lightweight HTTP processing layer.

```text
TCP Bytes
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
       HttpHandler
           │
           ▼
      HttpResponse
```

### 🧩 Custom Parser

The parser is stateful and incremental. It explicitly handles:

- request line
- headers
- body
- chunk size
- chunk data
- chunk-data CRLF
- trailers

It can return `NeedMoreData`, `Complete`, malformed-request, size-limit, transfer-encoding, and HTTP-version related results. fileciteturn75file0L21-L44

### 🛡️ Parser Limits

| Limit | Value |
|:--|--:|
| Request line | **8 KiB** |
| Individual header line | **8 KiB** |
| Total headers | **32 KiB** |
| Header count | **100** |
| Request body | **1 MiB** |

---

## 🔀 HTTP Methods & Routing

The HTTP layer supports:

| Method | Support |
|:---:|:---|
| `GET` | ✅ |
| `POST` | ✅ |
| `PUT` | ✅ |
| `DELETE` | ✅ |
| `HEAD` | ✅ |
| `OPTIONS` | ✅ |

`GET`, `POST`, `PUT`, and `DELETE` power the Task Manager API. `HEAD` and `OPTIONS` are handled by the HTTP layer; `OPTIONS` advertises the supported methods and `HEAD` suppresses the response body. fileciteturn74file2L343-L421

### Routes

```text
GET     /hello
GET     /health
GET     /metrics
GET     /tasks

POST    /tasks
PUT     /tasks/:id
DELETE  /tasks/:id
```

The server also supports static files through the `public` directory for `/` and file-like paths. fileciteturn74file2L328-L341

---

## 🌍 Layer-4 TCP Load Balancer

The load balancer operates at **Layer 4**. It forwards TCP connections between clients and backend servers without needing to parse HTTP application semantics.

```text
                     Layer 4
🌐 Client ─────► ⚡ AORTA LB ─────► 🖥️ Backend
                    TCP Proxy
                                      │
                                      ▼
                                  HTTP/1.1
                                  Parser
                                      │
                                      ▼
                                    Router
```

### 🔄 Routing

**Round Robin**

```text
Connection 1 → Server 1
Connection 2 → Server 2
Connection 3 → Server 3
Connection 4 → Server 1
...
```

**Consistent Hashing**

AORTA supports consistent-hash routing with a virtual-node ring. The routing mode is selected using `AORTA_LB_ROUTING`, with Round Robin as the default. fileciteturn76file4L363-L428

### ❤️ Health Checks & Failover

Backend health checking is integrated into the load balancer's event loop using timers and `epoll`. fileciteturn79file3L357-L375

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
Route To Healthy Backends
```

---

## 🧵 Multi-Reactor + Worker Pool

Each reactor owns its network-side connection state and event loop.

Blocking Redis operations are submitted to a worker pool:

```text
⚡ Reactor
    │
    ▼
Worker Queue
    │
    ├──► Worker
    ├──► Worker
    └──► Worker
            │
            ▼
         🔴 Redis
            │
            ▼
         eventfd
            │
            ▼
        ⚡ Reactor
```

The load balancer follows the same event-driven style, maintaining reactor state while handling client/backend events and health-timer events. fileciteturn79file3L332-L427

---

## 📝 Task Manager Web Application

The Task Manager is the **actual application running on top of AORTA**.

It gives the distributed infrastructure a real workload rather than serving only synthetic responses.

### 🖥️ What you can do

| Feature | Description |
|:--|:--|
| ➕ Create | Add tasks |
| ✅ Complete | Toggle completion |
| ✏️ Edit | Modify task data |
| 🗑️ Delete | Remove tasks |
| 💾 Persist | Store task state in Redis |
| 🔁 Distribute | Serve requests through the AORTA load balancer |

The frontend communicates with `/tasks` and provides task creation, editing, completion, and deletion. fileciteturn75file5L541-L637

### 🌐 Open the app

After starting the Docker stack:

**http://localhost:9000/**

That opens the **Task Manager UI**.

The JSON API is also available:

```text
GET     /tasks
POST    /tasks
PUT     /tasks/:id
DELETE  /tasks/:id
```

---

## 📊 Prometheus + Grafana Dashboard

AORTA includes a real **Prometheus + Grafana observability stack**.

```text
AORTA Services
      │
      │ metrics
      ▼
 Prometheus
      │
      │ queries
      ▼
  Grafana
```

The repository includes Prometheus configuration, Grafana datasource provisioning, and the AORTA dashboard definition. fileciteturn76file0L32-L42

### 📈 Dashboard panels

The dashboard includes:

- global throughput
- latency percentiles
- backend health
- backend status
- active connections
- backend connection metrics
- failover information

The dashboard definition contains global request/response throughput, p50/p95/p99 latency panels, and per-server health panels. fileciteturn77file4L197-L233

### 🌐 Open the dashboard

After the stack is running locally:

| Service | Address |
|:--|:--|
| 📝 Task Manager | `http://localhost:9000/` |
| 📈 Grafana | `http://localhost:3000` |
| 📡 Prometheus | `http://localhost:9090` |
| 📊 Raw metrics | `http://localhost:9000/metrics` |

> These are **local runtime addresses**. They become available on the machine running the Docker stack; they are not public hosted services.

---

# 🐳 Running AORTA

## 1. ✅ Check your environment

AORTA is intended to run with **Docker Compose**.

First check:

```bash
git --version
docker --version
docker compose version
```

You need Git, Docker, and Docker Compose available before starting.

Docker's official documentation recommends Docker Desktop as the simplest way to obtain Docker Engine, Docker CLI, and Docker Compose on supported desktop platforms; on Linux, Docker Compose is available as a CLI plugin. citeturn451182search0turn451182search2

---

## 2. 📦 Install Docker if it is missing

### 🐧 Arch Linux / Garuda Linux

```bash
sudo pacman -S --needed docker docker-compose
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```

Start a new shell session or run:

```bash
newgrp docker
```

Then verify:

```bash
docker --version
docker compose version
```

### 🐧 Ubuntu / Debian

For Docker's official packages:

```bash
sudo apt update
sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker
```

Then verify:

```bash
docker --version
docker compose version
```

Docker's current Ubuntu instructions install Docker Engine with the Compose plugin and recommend verifying the daemon is running. citeturn451182search5

### 🐧 Fedora

Configure Docker's repository, then install:

```bash
sudo dnf config-manager addrepo --from-repofile https://download.docker.com/linux/fedora/docker-ce.repo
sudo dnf install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker
```

Verify:

```bash
docker --version
docker compose version
```

### 🪟 Windows

Install **Docker Desktop**.

Start Docker Desktop, then open PowerShell:

```powershell
docker --version
docker compose version
```

Docker Desktop includes Docker Engine, Docker CLI, and Docker Compose. citeturn451182search4

### 🍎 macOS

Install **Docker Desktop**.

Start Docker Desktop, then open Terminal:

```bash
docker --version
docker compose version
```

Docker Desktop includes Docker Compose and the Docker Engine/CLI needed to run the stack. citeturn451182search0turn451182search4

---

## 3. 🚀 Clone AORTA

```bash
git clone https://github.com/Sri-Harshaaa/Aorta.git
cd Aorta
```

---

## 4. 🏗️ Build and start everything

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
📡 Prometheus
📊 Grafana
```

---

## 5. 🔎 Verify the deployment

```bash
docker compose ps
```

If something is not running:

```bash
docker compose logs
```

For individual services:

```bash
docker compose logs lb
docker compose logs server1
docker compose logs server2
docker compose logs server3
```

---

## 6. 🧪 Try AORTA

### ❤️ Health

```bash
curl http://localhost:9000/health
```

Expected:

```text
OK
```

### 📝 Task Manager

Open:

**http://localhost:9000/**

### 📊 Grafana

Open:

**http://localhost:3000**

### 📡 Prometheus

Open:

**http://localhost:9090**

### 📈 Metrics

```bash
curl http://localhost:9000/metrics
```

---

# 📋 Task API

### ➕ Create

```bash
curl -X POST http://localhost:9000/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA","description":"Finish the distributed system"}'
```

### 📋 Read

```bash
curl http://localhost:9000/tasks
```

### ✏️ Update

```bash
curl -X PUT http://localhost:9000/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA v2","description":"Improve performance"}'
```

### 🗑️ Delete

```bash
curl -X DELETE http://localhost:9000/tasks/1
```

---

# 🚀 Redis Optimization

The original task retrieval path used an N+1 access pattern:

```text
SMEMBERS tasks
     │
     ├── HGETALL task:1
     ├── HGETALL task:2
     ├── HGETALL task:3
     ├── ...
     └── HGETALL task:N
```

The optimized path uses a **single Lua `EVAL` operation**, reducing unnecessary Redis round trips.

---

# 📈 Performance

AORTA was benchmarked using [`wrk`](https://github.com/wg/wrk) under increasing client concurrency.

Example:

```bash
wrk --latency -t4 -c500 -d30s http://localhost:9000/tasks
```

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

> ⚠️ These are measurements from the tested build. Actual performance depends on hardware, operating system, Docker configuration, workload, and system state.

📄 Detailed benchmark report:

`docs/AORTA_Benchmark_Performance_Report.pdf`

---

# 🔬 Performance Engineering

AORTA was improved through a measurement-driven workflow:

```text
📏 Measure
    ↓
🔎 Identify bottleneck
    ↓
🛠️ Make one targeted change
    ↓
🧪 Benchmark again
    ↓
✅ Keep / ↩️ Revert
```

Key work:

- Redis N+1 retrieval → single Lua operation
- non-blocking sockets + `epoll`
- blocking Redis work → worker pool
- worker completion → `eventfd`
- avoided unnecessary `epoll_ctl` modifications

---

# 📁 Project Structure

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
├── LICENSE
└── README.md
```

---

# 🧱 Native Linux Build

For native Linux development:

```bash
cmake -S . -B build
cmake --build build -j
```

Generated binaries:

```text
build/aorta
build/aorta_lb
```

Build artifacts are excluded from version control.

---

# 🛑 Stop AORTA

Stop the stack:

```bash
docker compose down
```

Stop the stack and remove persistent volumes:

```bash
docker compose down -v
```

---

# 📚 Documentation

Detailed benchmark and performance analysis:

**`docs/AORTA_Benchmark_Performance_Report.pdf`**

---

<div align="center">

## ⚡ AORTA

**Networking · Concurrency · Distributed Systems · Performance**

*Built to understand what happens underneath.*

</div>

---

## 📜 License

AORTA is released under the **MIT License**.

See [`LICENSE`](LICENSE) for the full license text.
