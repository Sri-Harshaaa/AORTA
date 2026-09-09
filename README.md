<div align="center">

# 🫀 AORTA

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

**Custom TCP Load Balancer · Event-Driven HTTP Servers · Worker Pool · Redis · Prometheus · Grafana**

</div>

---

## 🧠 What is AORTA?

AORTA is a **high-concurrency distributed server system** built to explore how modern backend infrastructure handles networking, concurrency, persistence, failures, observability, and load.

At the center of the system is a **custom Layer-4 TCP load balancer** that distributes client connections across multiple **multi-reactor, event-driven HTTP servers**. The servers use Linux `epoll`, non-blocking sockets, a custom HTTP/1.1 parser and router, asynchronous worker execution, and Redis-backed persistence.

The Task Manager is the application workload running on top of this infrastructure, giving the system something real to serve, persist, distribute, monitor, and benchmark.

> **AORTA is built to understand the infrastructure underneath a web application — not just the application itself.**

---

## 🏗️ System Architecture

```text
                                      🌐 CLIENT
                                          │
                                          ▼
                              ┌───────────────────────┐
                              │    ⚡ AORTA LB         │
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


                    ┌─────────────────────────────────┐
                    │       📊 OBSERVABILITY          │
                    │                                 │
                    │ AORTA → Prometheus → Grafana   │
                    └─────────────────────────────────┘
```

### The high-concurrency pattern

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
              ┌──────────┴──────────┐
              │                     │
              ▼                     ▼
        Non-blocking work      Blocking Redis work
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

The core design is to keep **network I/O event-driven** while moving blocking Redis work away from the reactor threads.

---

## 🌐 Networking Layer

AORTA implements the networking path directly using Linux sockets and `epoll`.

The server lifecycle includes the familiar socket operations:

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
non-blocking socket
   ↓
epoll
   ↓
recv() / send()
   ↓
close()
```

Connections are registered with `epoll`, tracked by the reactor, and given connection deadlines so inactive connections can be removed.

### ⚡ Why epoll?

Instead of:

```text
1 connection → 1 thread
```

AORTA uses:

```text
many connections
       ↓
   one reactor
       ↓
      epoll
       ↓
ready sockets only
```

This is the event-driven model used as the foundation of the high-concurrency server architecture.

---

## 🌐 HTTP/1.1 Engine

AORTA contains its own lightweight HTTP processing layer rather than depending on a web framework.

```text
TCP Byte Stream
      │
      ▼
┌───────────────────────┐
│     HTTP Parser       │
│                       │
│  Request Line         │
│  Headers              │
│  Body                 │
│  Chunked Encoding     │
│  Trailers             │
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

### 🧩 Custom HTTP parser

The parser maintains explicit states for:

- request line
- headers
- body
- chunk-size
- chunk data
- chunk-data CRLF
- trailers

It also supports incremental parsing when more TCP data is required.

### 🛡️ Parser limits

| Limit | Value |
|:--|--:|
| Maximum request line | **8 KiB** |
| Maximum individual header line | **8 KiB** |
| Maximum total header size | **32 KiB** |
| Maximum header count | **100** |
| Maximum request body | **1 MiB** |

The parser has explicit outcomes for incomplete input, malformed requests, oversized requests/headers/body, unsupported transfer encoding, and unsupported HTTP versions.

### 🔀 HTTP methods

AORTA's HTTP layer handles:

| Method | Support |
|:---:|:---|
| `GET` | ✅ |
| `POST` | ✅ |
| `PUT` | ✅ |
| `DELETE` | ✅ |
| `HEAD` | ✅ |
| `OPTIONS` | ✅ |

`GET`, `POST`, `PUT`, and `DELETE` power the Task Manager API. `HEAD` and `OPTIONS` are handled at the HTTP layer, with `OPTIONS` advertising the supported methods and `HEAD` returning headers without the response body.

---

## 🔀 HTTP Routing & Handlers

After parsing, requests are passed into the router and then to the appropriate handler.

The current HTTP layer exposes:

```text
GET  /hello
GET  /health
GET  /metrics
GET  /tasks

POST /tasks
PUT  /tasks/:id
DELETE /tasks/:id
```

The server also supports serving static files from its `public` directory for the root path and file-like requests.

---

## 🧵 Multi-Reactor Architecture

AORTA is structured around a **multi-reactor server model**.

Each reactor owns its network-side connection state and runs an event loop. Worker threads are used for operations that should not block that event loop.

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

The load balancer follows the same event-driven approach and maintains reactor state while handling client/backend events, health timers, and connection pairs.

---

## 🧵 Worker Pool & Asynchronous Redis

Redis operations are separated from the network reactor.

```text
Request
   │
   ▼
⚡ Reactor
   │
   │ submit
   ▼
Worker Queue
   │
   ├────────► Worker
   ├────────► Worker
   └────────► Worker
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

This keeps the reactor focused on handling network events rather than waiting on blocking storage operations.

---

## 🌍 Layer-4 TCP Load Balancer

The load balancer operates at **Layer 4**, forwarding TCP traffic between clients and backend servers.

That gives AORTA a clean separation:

```text
                Layer 4                         Application Layer

🌐 Client ───── TCP ─────► ⚡ AORTA LB ───── TCP ─────► 🖥️ Backend
                                                        │
                                                        ▼
                                                   HTTP Parser
                                                        │
                                                        ▼
                                                     Router
```

The load balancer does not need to parse the HTTP application protocol in order to forward the connection.

### 🔄 Routing algorithms

#### Round Robin

```text
Connection 1 → Server 1
Connection 2 → Server 2
Connection 3 → Server 3
Connection 4 → Server 1
...
```

#### 🧭 Consistent Hashing

AORTA can build a consistent-hash ring with virtual nodes and use hashing-based routing. The routing mode is selected through `AORTA_LB_ROUTING`; Round Robin is the default, while `consistent_hash`, `consistent-hash`, or `hash` selects consistent hashing.

---

## ❤️ Health Checks & Failover

Backends are actively monitored by the load balancer.

```text
                 Backend Healthy
                        │
                        ▼
                  Normal Routing
                        │
                     failure
                        │
                        ▼
                  Health Check
                        │
                        ▼
               Mark Backend Unhealthy
                        │
                        ▼
                Remove From Rotation
                        │
                        ▼
                Route to healthy nodes
```

Health checking is integrated into the load balancer's event-driven loop through a timer and `epoll`.

---

## 📝 Task Manager Application

AORTA includes a **Task Manager web application** as the real application running on top of the infrastructure.

It is intentionally simple so that the interesting part of the project remains the server architecture.

### 🖥️ What the application supports

| Capability | Description |
|:--|:--|
| ➕ Create task | Add a new task |
| ✅ Complete task | Toggle completion state |
| ✏️ Edit task | Update task information |
| 🗑️ Delete task | Remove a task |
| 💾 Persistence | Store tasks in Redis |
| 🔁 Distributed access | Access the application through the load balancer |

The frontend talks to the `/tasks` API and provides task creation, editing, completion, and deletion.

### 🌐 Open the application

After starting AORTA with Docker Compose, open this **local address in your browser**:

**http://localhost:9000/**

That opens the actual Task Manager UI.

The same application can also be exercised directly through the JSON API:

```text
GET     /tasks
POST    /tasks
PUT     /tasks/:id
DELETE  /tasks/:id
```

---

## 📊 Prometheus + Grafana

AORTA includes a real monitoring stack.

```text
AORTA Services
      │
      │ metrics
      ▼
┌─────────────┐
│ Prometheus  │
└──────┬──────┘
       │
       │ queries
       ▼
┌─────────────┐
│   Grafana   │
└─────────────┘
```

The repository contains Prometheus configuration, Grafana datasource provisioning, and the dashboard definition.

### 📈 What the dashboard shows

The dashboard is designed to answer operational questions such as:

- Are all backend servers healthy?
- How much traffic is being handled?
- What are the latency percentiles?
- How many connections are active?
- Which backend is healthy?
- Have any backend failovers occurred?

The dashboard includes global throughput, latency percentile graphs, backend health cards, and connection-related panels.

### 🌐 Open the monitoring tools

Once the stack is running locally:

| Tool | Local address |
|:--|:--|
| ⚡ AORTA / Task Manager | `http://localhost:9000/` |
| 📡 Prometheus | `http://localhost:9090` |
| 📊 Grafana | `http://localhost:3000` |
| 📈 Raw AORTA metrics | `http://localhost:9000/metrics` |

> These are **local runtime addresses**. They work on the machine running the Docker stack; they are not links to a remotely hosted AORTA instance.

---

# 🖥️ Platform Requirement

AORTA uses Linux-specific networking primitives including `epoll`, `accept4`, and `SO_REUSEPORT`. Native execution is intended for Linux. On Windows, use WSL2 or Docker Desktop; on macOS, use Docker Desktop for the complete Docker Compose stack.

---

# 🐳 Running AORTA

## 1. ✅ Check your environment

AORTA's recommended deployment is Docker Compose.

Check Git:

```bash
git --version
```

Check Docker:

```bash
docker --version
```

Check Docker Compose:

```bash
docker compose version
```

You need all three commands to work before starting the project.

---

## 2. 📦 Install Docker if it is missing

### 🐧 Arch Linux / Garuda Linux

```bash
sudo pacman -S --needed docker docker-compose
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```

Then start a new shell session, or run:

```bash
newgrp docker
```

Verify:

```bash
docker --version
docker compose version
```

Arch's Docker documentation recommends installing Docker, starting/enabling the Docker service, and installing the `docker-compose` package for Compose projects.

### 🐧 Ubuntu / Debian

Install Docker Engine and the Compose plugin:

```bash
sudo apt update
sudo apt install docker.io docker-compose-plugin
sudo systemctl enable --now docker
```

Verify:

```bash
docker --version
docker compose version
```

For the official Docker Engine repository installation, Docker provides distribution-specific instructions; the Compose plugin is installed as `docker-compose-plugin`.

### 🐧 Fedora

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

These are based on Docker's current Fedora installation instructions.

### 🪟 Windows

Install **Docker Desktop**, start it, then open PowerShell and verify:

```powershell
docker --version
docker compose version
```

Docker Desktop includes Docker Engine, Docker CLI, and Docker Compose.

### 🍎 macOS

Install **Docker Desktop**, start Docker Desktop, then verify in Terminal:

```bash
docker --version
docker compose version
```

Docker Desktop is the recommended way to obtain Docker Compose on macOS.

---

## 3. 🚀 Clone AORTA

```bash
git clone https://github.com/Sri-Harshaaa/Aorta.git
cd Aorta
```

---

## 4. 🏗️ Build and start the entire stack

Run:

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

## 5. 🔎 Verify that everything is running

```bash
docker compose ps
```

For logs:

```bash
docker compose logs
```

For a particular service:

```bash
docker compose logs lb
docker compose logs server1
docker compose logs server2
docker compose logs server3
```

---

## 6. 🧪 Try the application

### ❤️ Health check

```bash
curl http://localhost:9000/health
```

Expected:

```text
OK
```

### 🖥️ Open Task Manager

Open in a browser:

**http://localhost:9000/**

### 📊 Open Grafana

Open:

**http://localhost:3000**

#### 🔐 Grafana Login

Use the following default credentials:

```text
Username: admin
Password: admin
```

#### 🧭 Open the AORTA Operations Dashboard

1. Open **Grafana** at `http://localhost:3000`.
2. Log in with the credentials above.
3. Click **Dashboards** in the Grafana navigation.
4. Select the **AORTA** folder.
5. Select **AORTA — Operations Dashboard**.

### 📡 Open Prometheus

Open:

**http://localhost:9090**

### 📈 Check raw metrics

```bash
curl http://localhost:9000/metrics
```

---

# 📋 Task API

The Task Manager exposes a simple CRUD API. Task objects use `id`, `title`, and `completed`; there is no `description` field in the current API.

| Method | Endpoint | Description |
|:---:|:---|:---|
| `GET` | `/tasks` | Get all tasks |
| `POST` | `/tasks` | Create a task |
| `PUT` | `/tasks/:id` | Update a task |
| `DELETE` | `/tasks/:id` | Delete a task |

### ➕ Create

```bash
curl -X POST http://localhost:9000/tasks \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA","completed":false}'
```

### 📋 Read

```bash
curl http://localhost:9000/tasks
```

### ✏️ Update

```bash
curl -X PUT http://localhost:9000/tasks/1 \
  -H "Content-Type: application/json" \
  -d '{"title":"Build AORTA v2","completed":true}'
```

### 🗑️ Delete

```bash
curl -X DELETE http://localhost:9000/tasks/1
```

---

## 🚀 Redis Optimization

The task retrieval path originally used an N+1 access pattern:

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

## 🧪 Benchmark Tool: `wrk`

Before running any benchmark command, first check that `wrk` is installed:

```bash
wrk --version
```

If `wrk` is not installed, use the appropriate installation command below.

### Arch Linux / Garuda Linux

```bash
sudo pacman -S wrk
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install wrk
```

### Fedora

```bash
sudo dnf install wrk
```

### macOS

```bash
brew install wrk
```

### Windows

Run `wrk` through WSL2 or another Linux environment.

After installation, verify it again:

```bash
wrk --version
```

---

# 📈 Performance

AORTA was benchmarked with [`wrk`](https://github.com/wg/wrk) under increasing client concurrency. The tables below are the latest latency-characterization runs; selected peak throughput figures are also reported separately in the benchmark report.

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

> ⚠️ These are measurements from the tested build. Actual results depend on CPU, operating system, Docker configuration, workload, and system state.

The benchmark report also records an earlier optimized `/tasks` run reaching approximately **28.9K requests/sec**, while direct backend testing reached approximately **35.8K requests/sec** at c500.

📄 Detailed benchmark report:

`docs/AORTA_Benchmark_Performance_Report.pdf`

---

## 🔬 Performance Engineering

AORTA was optimized using a measurement-driven workflow:

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

Key work included:

- Redis N+1 retrieval → single Lua operation
- non-blocking sockets + `epoll`
- blocking Redis work → worker pool
- worker completion → `eventfd`
- avoiding unnecessary `epoll_ctl` modifications

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

# 🛑 Stop the Stack

Stop the running services:

```bash
docker compose down
```

Stop the services and remove persistent volumes:

```bash
docker compose down -v
```

---

# 📚 Documentation

For the detailed benchmark and performance analysis:

**`docs/AORTA_Benchmark_Performance_Report.pdf`**

---

<div align="center">

## 🫀 AORTA

**Networking · Concurrency · Distributed Systems · Performance**

*Built to understand what happens underneath.*

</div>

---

## 📜 License

AORTA is released under the **MIT License**.

See the [`LICENSE`](LICENSE) file for the full license text.
