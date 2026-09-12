# Performance investigation

Run from the repository root. Requires Linux, cmake, a C++20 compiler, hiredis,
wrk, taskset, pidstat, mpstat, perf, strace, curl and Python 3.

```
cmake -S . -B build/perf-investigation -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O2 -g -DNDEBUG -fno-omit-frame-pointer'
cmake --build build/perf-investigation -j8
cp build/perf-investigation/aorta build/perf-investigation/aorta-before
python3 benchmarks/run.py before
python3 benchmarks/profile.py profile-before
# Apply the change and rebuild using the same command/flags.
python3 benchmarks/run.py after
python3 benchmarks/profile.py profile-after
python3 benchmarks/summarize.py before after
```

The harness uses port 18080. Stop other benchmark runs first. Affinities are
specific to the measured 16-logical-CPU machine: CPUs 0-7 and 8-15 are disjoint
sets of four physical cores each, including both SMT siblings. Adapt both
scripts for another machine and use identical settings for both binaries.
The server still creates its default 16 reactors and 8 workers. This is a
fixed-resource experiment, not a claim about maximum full-machine capacity.

Each matrix has three repetitions of c100, c500, c1000, c5000 in that order,
a separate 3-second warmup at each level, then a 15-second measured run.
Warmup and measured runs create separate connections. wrk uses eight threads,
HTTP/1.1 keep-alive, no pipelining and a five-second timeout. Its implementation
may round connections down to a multiple of threads (96 and 496 for c100/c500).
CPU percentages count one logical CPU as 100%; process CPU seconds come from
/proc/PID/stat. Raw thread, per-core, latency, throughput and error output is
retained. Monitor overhead is present equally before and after. Profiling runs
are separate and excluded from the benchmark matrix; strace perturbs execution
heavily, so its elapsed timing must not be treated as untraced CPU attribution.

The primary endpoint is /hello (16-byte response body), isolating reactor,
parser, router, metrics and socket work. Redis is lazy-connected and is not
required for this endpoint. /tasks has an additional queue/Redis/completion path;
results for /hello do not establish its storage bottleneck or production SLOs.
wrk is a closed-loop load generator; reported latency is not an open-loop
arrival-rate test and can hide overload through coordinated omission.

Request path:

* Optional LB: each LoadBalancer owns a reuseport listener and epoll loop;
  handleAccept records the accepted socket. On first input, start peeks at the
  request prefix: GET /metrics is served by the LB itself; other traffic invokes
  connectClientToBackend, selects a backend and starts a nonblocking connection.
  start handles client/backend readiness, forwardData receives into per-pair
  buffers, flushData sends, and updateWriteInterest tracks EPOLLOUT. HealthChecker
  performs timed asynchronous checks; backend/routing state is per LB reactor.
* Server::start launches reactors sharing Metrics and WorkerPool. Reactor::run
  registers its listener, timerfd, completion eventfd and accepted client sockets
  in a level-triggered epoll instance. SO_REUSEPORT distributes connections.
* Reactor::handleClient drains Connection::read until EAGAIN, incrementally calls
  HttpParser::parse, copies the completed HttpRequest, updates metrics and invokes
  HttpHandler::handle / HttpRouter. Synchronous responses are serialized and
  queued; static files additionally use stat/open/sendfile. Parser state resets
  for keep-alive/pipelined requests. Deadlines are scanned once per second.
* /tasks: TaskManager submits to the mutex/condition-variable WorkerPool queue;
  worker-local hiredis clients synchronously execute Redis Lua commands. A
  per-reactor completion queue and eventfd return callbacks to the owning reactor;
  completeAsyncResponse checks the connection generation before queuing output.
* Baseline output: handleClient enables EPOLLOUT; a later readiness event calls
  handleWrite / Connection::write (send MSG_NOSIGNAL or sendfile), then removes
  EPOLLOUT. Incomplete output remains queued. Close/error paths release the fd.

The built-in latency metric starts after parsing and stops before serialization
and socket transmission, so it is not end-to-end latency. Byte metrics are
exposed but Connection does not call their update methods; use wrk for traffic.

Additional workload characterization (uses an existing local redis:7-alpine image):

```
benchmarks/secondary.sh before build/perf-investigation/aorta-before
benchmarks/secondary.sh after build/perf-investigation/aorta
python3 benchmarks/profile_tasks.py
python3 tests/reactor_output.py
```

The secondary script uses a disposable, loopback-only Redis on port 16379,
seeds exactly 100 tasks, and removes the container afterward. Redis shares the
client CPU set. Each secondary matrix uses one 10-second run per concurrency,
with the same warmup. The LB check uses one backend and four LB reactors sharing
the server CPU set, with a private working directory/config; it does not edit
config/backends.conf. It is a controlled proxy-overhead check, not a reproduction
of the three-backend Docker deployment. CPU values for that check are backend
process CPU only; mpstat includes the LB. /tasks latency at c5000 mixes successes
and errors; use successful response rate, not total wrk throughput, to judge it.

The baseline source revision is cf9e961933da9951ac66cc6e9ccc6bc5c6967556. The
commands above assume the first build is from that revision, before applying
the fix. The original executable has been retained locally as aorta-before.
To repeat the later drift check using these binaries:

```
python3 benchmarks/run.py before-validation --binary build/perf-investigation/aorta-before --repeats 1
python3 benchmarks/run.py after-validation --repeats 1
```

Use fresh labels to preserve existing result directories. The test defaults to
expecting an unavailable Redis (HTTP 503). Set AORTA_REDIS_PORT and
AORTA_EXPECT_TASK_STATUS=200 to check a healthy Redis; profile_tasks.py does this
with its isolated fixture. The profile and benchmark scripts must run sequentially.
