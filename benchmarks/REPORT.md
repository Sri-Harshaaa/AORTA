# AORTA performance investigation

## Remaining-system investigation (2026-09-13)

This section completes the load-balancer, Redis `/tasks`, same-host process
scaling, and end-to-end work left open by the HTTP fast-path investigation
below. The starting revision was `a3ab199`; its immediate-write server fast
path is unchanged.

### Method and limits

All capacity tables are medians of three runs. Each cell used an independent
2-second warmup followed by a 5-second measurement, eight wrk threads,
HTTP/1.1 keep-alive, a five-second timeout, and identical affinity and
concurrency within a comparison. Raw wrk and component CPU data are under
`results/rest-*`. CPU is process CPU (100% is one logical CPU); the LB
representative run additionally retains pidstat and per-CPU mpstat. Profiling
and strace runs were separate and are not capacity measurements.

The host has eight physical Zen 3 cores and SMT (16 logical CPUs). Logical
siblings are adjacent. Direct/LB tests assigned backend CPUs 0-3, LB CPUs 4-7,
and client CPUs 8-15. The same-host process-scaling test assigned all servers
CPUs 0-7 and the client CPUs 8-15. The end-to-end task test assigned servers
0-3, LB 4-5, Redis 6-7, and client 8-15. Frequency scaling, a shared desktop,
short samples, and loopback introduce visible drift; these results establish
bottleneck order and causal changes, not production capacity.

### Confirmed bottleneck hierarchy

1. **Redis is the end-to-end `/tasks` ceiling.** With three backends and the
   LB, Redis remains at about 94% of one CPU while the LB is only 30-37% and
   the three AORTA processes total about 201-205%. Median useful throughput is
   about 9.5k req/s through c1000.
2. **The LB/kernel relay path caps proxied `/hello`.** A four-reactor LB reaches
   about 151k req/s at c1000 versus 223k direct. Its assigned CPUs are 99% busy:
   approximately 54% system and 39% softirq each. Adding backend processes does
   not raise throughput.
3. **The web server and local load generator co-limit the direct fast path.**
   On four physical server cores, one process reaches about 347k req/s at
   c1000. At c5000 the client consumes about 546% CPU versus server 485%, so
   wrk/network processing is explicitly a limiting component there.
4. **Extra same-host processes do not scale this server.** With a fixed CPU
   budget they add SO_REUSEPORT/event-loop scheduling overhead and reduce both
   aggregate throughput and RPS/process-CPU-core. This is same-host multicore
   scaling, not horizontal scaling.

### L4 load balancer

#### Direct versus proxy

The selected LB configuration uses four reactors, which was the best tested
match for its four-logical-CPU affinity. All runs completed without socket or
HTTP errors.

| Connections | Direct req/s | LB req/s | Proxy loss | Direct mean / p99 ms | LB mean / p99 ms | Direct server CPU % | LB backend / LB CPU % |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 213,814 | 180,101 | -15.8% | 1.10 / 5.91 | 0.53 / 0.71 | 286.6 | 238.2 / 240.4 |
| 500 | 239,534 | 171,892 | -28.2% | 2.43 / 8.64 | 2.87 / 3.27 | 281.2 | 230.4 / 237.7 |
| 1000 | 223,184 | 151,210 | -32.2% | 4.58 / 12.48 | 6.54 / 7.77 | 280.5 | 209.0 / 239.6 |
| 5000 | 196,094 | 131,555 | -32.9% | 25.04 / 45.79 | 37.46 / 60.12 | 275.8 | 177.7 / 255.5 |

The direct and proxy matrices ran at different times; the anomalous direct
c100 latency and broad direct throughput range are host-drift evidence, not a
claim that proxying improves latency. The throughput gap is stable at the
higher concurrency levels where both paths are loaded.

#### Profiler and syscall evidence

In the baseline c500 perf capture, the two connection hash-table lookups
account for 5.54% and 4.72% of user-cycle samples. `forwardData`/`flushData`
lead into the unresolved kernel samples. A separate connection-churn profile
finds `getaddrinfo` at 4.02%, malloc at 3.36%, free at 2.62%, and
`connectToBackend` at 1.51%; those are setup costs, not steady-state
keep-alive costs. Backend selection is once per client connection. Health
checking is once per five seconds per reactor/backend and does not appear in
the >=0.5% steady-state profile. LB routing/metrics state is reactor-local, so
there is no shared request-path lock or metric atomic to remove.

The baseline traced LB relayed 48,647 responses using 98,248 sends, 98,984
receives, and 3,229 epoll controls. The final traced LB relayed approximately
111,960 responses using 223,920 sends, 224,966 receives, and 3,231 epoll
controls. Thus the LB itself requires about two sends and two receives per
response; the backend adds its own send and receive/EAGAIN pair. Epoll controls
are connection setup/teardown and backpressure work, not two unconditional
operations per response. In the final trace, send/receive account for 96.2%
of summed traced syscall time. A direct-server trace uses one send and two
receives (the second is EAGAIN) per response.

The representative untraced four-reactor run records 152,713 req/s, LB process
CPU 239%, and backend CPU 213%. CPUs 4-7 have only 0.75-0.88% idle; each spends
about 54% in system time and 39% in softirq. The kernel/socket relay is therefore
the capacity cost hidden by process-only CPU. Perf lacked kernel symbols, so no
finer kernel-function attribution is claimed.

#### Reactor and backend scaling

| LB reactors (same CPUs) | c1000 req/s | p99 ms | Backend CPU % | LB CPU % |
|---:|---:|---:|---:|---:|
| 1 | 50,965 | 21.90 | 67.5 | 62.3 |
| 2 | 76,727 | 17.39 | 146.5 | 123.0 |
| 4 | 151,386 | 8.00 | 208.8 | 239.4 |

Four reactors remove reactor under-provisioning, but backend count still does
not scale throughput:

| Backends | c1000 req/s | p99 ms | Aggregate backend CPU % | LB CPU % | Request distribution |
|---:|---:|---:|---:|---:|---:|
| 1 | 151,210 | 7.77 | 209.0 | 239.6 | 100% |
| 2 | 147,316 | 7.90 | 223.7 | 236.0 | 50.1% / 49.9% |
| 3 | 144,420 | 8.55 | 229.7 | 235.2 | 33.4% / 33.2% / 33.3% |

Round-robin distribution is correct, but the LB CPU set is saturated before
the backend CPU set. The measured LB limit for this host/configuration is about
150k small responses/s at c1000; adding servers cannot cross it.

#### LB change and rejected optimization

The connection-close workload exposed a correctness defect: Linux can deliver
`EPOLLIN|EPOLLRDHUP` (or HUP) together, while the LB previously half-closed the
destination after only one read. That truncated buffered responses. Before the
fix, an eight-second c100 `Connection: close` run completed 7,677 responses and
reported 62,386 read errors (948 req/s). After the fix it completed 56,018
responses without errors (6,917 req/s). The LB now drains on peer-close, defers
`SHUT_WR` until the corresponding output buffer is empty, and tracks each
write-half closure.

This is intentionally not presented as a keep-alive throughput optimization.
The identical two-reactor keep-alive c500 median was 90,654 req/s before and
81,051 req/s after amid host drift. A subsequent experiment removed redundant
map rechecks because perf showed hash lookups, but its c500 median was 82,260
req/s and it did not reduce LB CPU. That experiment was reverted. DNS caching
and zero-copy relay were also not introduced: DNS was only a connection-churn
secondary cost, caching changes address-refresh semantics, and splice/io_uring
would be an architectural change without measured evidence here.

### `/tasks` and Redis

#### Root cause and change

The original single-round-trip Lua script executed `SMEMBERS`, then 100
interpreted `HGETALL` operations for every 100-task response. Across the
baseline matrix Redis records 267,759 EVAL calls, 26,775,800 HGETALL calls, and
304.15 microseconds/EVAL. Redis stays at about 98% CPU while AORTA is only
68-74% at c100-c1000. This rules out the worker mutex, reactor callbacks, and
JSON serialization as the original saturation cause.

`RedisClient::getAllTasks` now issues one native Redis command:
`SORT tasks BY nosort GET # GET task:*->title GET task:*->completed`. It keeps
the existing set/hash schema, one network round trip, missing-record filtering,
and the existing C++ numeric-id sort. Across the after matrix Redis records
740,038 SORT calls at 92.59 microseconds/call. No cache or consistency tradeoff
was introduced.

| Connections | Successful req/s before | Successful req/s after | Change | Mean ms before / after | p99 ms before / after | AORTA CPU % before / after | Redis CPU % before / after | Error % before / after |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 3,241 | 9,304 | +187% | 29.46 / 10.28 | 34.15 / 11.63 | 74.1 / 175.8 | 98.0 / 94.5 | 0 / 0 |
| 500 | 3,396 | 9,808 | +189% | 143.16 / 50.03 | 192.76 / 53.32 | 70.6 / 173.7 | 97.9 / 94.7 | 0 / 0 |
| 1000 | 3,258 | 9,593 | +194% | 295.72 / 102.36 | 462.03 / 107.41 | 68.1 / 175.0 | 97.9 / 94.6 | 0 / 0 |
| 5000 | 1,822 | 5,331 | +193% | 309.30 / 166.41 | 1,580 / 703 | 288.9 / 308.6 | 95.9 / 72.8 | 98.90 / 95.72 |

At c5000, total response rate is 165,534 before and 124,542 after, but almost
all are bounded-queue 503 responses. Only successful RPS above is useful task
throughput; the mixed wrk latency distribution includes both successes and
fast rejections.

The c500 user-cycle profile changes from 3,734 req/s and p99 147 ms to 12,174
req/s and p99 43 ms in the separate profiled runs. Before, hiredis reply parsing
is 6.62%, free/malloc 5.39%/4.59%, response JSON construction 3.75%, JSON
escaping 3.19%, and `getAllTasks` 2.94%. After the Redis improvement, JSON
construction rises to 7.00%, escaping to 5.68%, free/malloc to 5.83%/3.92%,
hiredis reply parsing to 4.52%, and `getAllTasks` to 3.73%. These are now
secondary CPU costs, but Redis is still the first saturated component, so no
unproven serialization micro-optimization was made.

#### Result-size sensitivity and pagination

| Tasks returned | Baseline req/s / p99 ms | After req/s / p99 ms | After AORTA / Redis CPU % |
|---:|---:|---:|---:|
| 10 | 24,802 / 4.33 | 54,508 / 2.09 | 281.6 / 77.4 |
| 100 | 3,239 / 34.15 | 9,308 / 11.63 | 175.8 / 94.5 |
| 1000 | 335 / 442.38 | 1,281 / 75.33 | 127.4 / 99.1 |

The native command is substantially faster at every size, but full-list cost
still grows with the result set and Redis returns to 99% CPU at 1,000 tasks.
The API has no pagination. Adding it safely requires an ordering/cursor and
response-contract decision; changing the default `/tasks` response would be a
breaking API change, so it remains the highest-impact tasks follow-up rather
than being guessed during this performance patch.

### Same-host server process scaling (no LB)

The first table uses unchanged defaults: every process creates 16 reactors and
8 workers even though all processes share CPUs 0-7.

| Processes | c1000 req/s | p99 ms | Server CPU % | RPS/process-CPU-core | Client CPU % | c5000 req/s / p99 ms |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 347,484 | 6.82 | 537.0 | 64,712 | 541.9 | 299,626 / 27.96 |
| 2 | 276,622 | 9.05 | 539.0 | 51,321 | 522.7 | 249,605 / 32.21 |
| 4 | 242,524 | 10.66 | 544.1 | 44,575 | 504.4 | 221,773 / 35.96 |

To isolate process count from event-loop count, a second matrix holds the total
at four reactors/four workers: 4/4 for one process, 2/2 each for two, and 1/1
each for four.

| Processes | c1000 req/s | p99 ms | Server CPU % | RPS/process-CPU-core | c5000 req/s / p99 ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 302,165 | 6.30 | 279.3 | 108,168 | 285,613 / 22.95 |
| 2 | 260,874 | 7.18 | 279.7 | 93,278 | 230,654 / 31.01 |
| 4 | 222,809 | 8.30 | 276.0 | 80,734 | 209,555 / 31.09 |

Extra processes reduce aggregate RPS in both matrices. SO_REUSEPORT balances
connections but does not create capacity on a fixed host. `AORTA_REACTORS` and
`AORTA_WORKERS` controls were added with unchanged hardware-derived defaults so
deployments and affinity tests can avoid accidental oversubscription. The
RPS/core column divides by measured process CPU and excludes softirq, so it is
a process-efficiency metric rather than total machine efficiency.

The single-process c1000 client and server each consume about 5.4 logical CPUs.
At c5000 the client is higher (545.7% versus server 484.8%), making local wrk/
loopback processing a co-bottleneck and preventing a clean server-only maximum.

### End-to-end: client -> LB -> 3 AORTA servers -> Redis

The Redis-backed topology used three distinct backend ports, two LB reactors,
and one isolated Redis. Two LB reactors are sufficient here because `/tasks`
throughput is an order of magnitude below the LB `/hello` ceiling.

| Connections | Successful req/s | Mean ms | p99 ms | 3-server CPU % | LB CPU % | Redis CPU % | Client CPU % | Errors | Distribution |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 9,334 | 10.25 | 12.84 | 205.1 | 30.1 | 93.5 | 33.9 | 0 | 33.3/33.3/33.3% |
| 500 | 9,621 | 50.99 | 62.01 | 201.1 | 33.5 | 93.7 | 34.4 | 0 | 33.3/33.3/33.3% |
| 1000 | 9,502 | 103.13 | 112.42 | 200.9 | 33.7 | 93.9 | 35.2 | 0 | 33.3/33.4/33.3% |
| 5000 | 8,568 | 434.96 | 721.43 | 201.1 | 36.9 | 93.6 | 37.2 | 0 | 33.4/33.4/33.3% |

Redis reports 791,678 SORT calls at 91.71 microseconds/call and an ending
instantaneous rate of 8,808 ops/s. Redis saturates first. Adding backend queues
avoids the single-process c5000 rejection storm, but cannot raise storage
throughput; it instead permits more queueing and much higher tail latency.

### Correctness and retained evidence

RelWithDebInfo and ASan/UBSan builds, their socket test suites, and
`git diff --check` pass. The original reactor regression still
passes keep-alive, HEAD, fragmented and pipelined input, connection close, a
6 MiB paused reader with EAGAIN/EPOLLOUT resumption, and async callbacks. New
task tests pass empty list, create, native listing, update, and delete against
isolated Redis. New LB tests pass keep-alive, fragmented/pipelined relay,
connection-close draining, balanced distribution, a verified 2 MiB paused
reader, client disconnect, initial-connect failover, and health-driven removal.

Key artifacts:

* `results/rest-lb-profile/`: perf, strace, pidstat, mpstat and raw wrk.
* `results/rest-tasks-profile/`: before/after perf and raw wrk.
* `results/rest-direct-baseline/`, `rest-lb-r4-backends*/`: direct/proxy matrices.
* `results/rest-tasks-baseline/`, `rest-tasks-after/`: Redis matrices and INFO.
* `results/rest-scale*/`: default and controlled-thread process scaling.
* `results/rest-e2e-tasks/`: full topology data and Redis INFO.
* `results/rest-*-correctness.txt`: correctness outputs.

Remaining bottlenecks are the LB's kernel TCP relay cost for small responses,
Redis's single-threaded full-list operation and missing API pagination, and the
local load generator on the direct high-rate path. Remote load generation,
multiple hosts, Redis clustering/read replicas, TLS, and a pagination contract
remain outside this measured same-host investigation.

---

## Earlier HTTP fast-path investigation

The direct HTTP fast path spends avoidable work scheduling writes: every small
response enables EPOLLOUT, waits for another readiness dispatch, writes, then
disables EPOLLOUT. The targeted fix attempts the nonblocking write immediately,
keeps EPOLLOUT for backpressure, and caches each connection's current interest mask.
It preserves the multi-reactor architecture, thread counts, parser, metrics,
worker queues and load-balancer implementation.

This is a workload-specific finding. The 100-task Redis listing has a different
limit, and a local wrk client becomes a constraint after the HTTP fix. There is
no evidence for a single universal bottleneck across all endpoints.

## Method and request path

See [README.md](README.md) for the complete request path and exact reproduction
commands, and [results/environment.txt](results/environment.txt) for hardware,
compiler flags, source revision and binary hashes. The worktree was clean before
investigation. Both executables built successfully using CMake and the same
-O2, debug-symbol, frame-pointer build configuration.

The primary matrix uses GET /hello over loopback, eight wrk threads, c100/c500/
c1000/c5000, a separate 3-second warmup and 15-second measurement, repeated three
times. Server and client run on separate four-physical-core CPU sets (both SMT
siblings); the default 16 reactors and 8 workers are unchanged. File-descriptor
limit is 1,048,576. Every primary run's raw wrk, per-thread pidstat, per-core
mpstat and process CPU measurements is retained under results/before and
results/after. No tracing runs overlap the primary benchmarks.

The warmup establishes separate connections, so startup/accept work is included
in each measured run. Fixed ascending concurrency order, a shared desktop host,
dynamic CPU frequency, short measurements and a local client limit precision.
These numbers are not an isolated-host capacity certificate. Later reruns of
the saved original binary check whether the observed improvement survives drift.

## Evidence and ranking

1. **Avoidable write readiness and epoll control work: highest-priority removable
   HTTP fast-path cost.** Baseline strace records 343,781 epoll_ctl calls for
   170,903 sendto calls: about 2.01 control calls per response. Source inspection
   then identifies the exact enable/defer/write/disable sequence in
   Reactor::handleClient, handleWrite and updateEvents. Most untraced server CPU
   time is system time; server cores also spend substantial time in softirqs.
   The intervention and post-change syscall counts test this causal hypothesis.
2. **Socket/network CPU and scheduling: a real capacity limit, still present.**
   recvfrom is called about twice per response (one receives data, one returns
   EAGAIN); sendto once. At c5000, baseline server cores have less than 1% idle
   in a representative run and reactors show substantial scheduler wait.
   Sixteen reactors share eight logical CPUs by design of this fixed-resource
   benchmark. The fix does not tune thread count or alter the receive loop.
   Afterward, client CPU saturation caps the local test, especially at c5000.
3. **HTTP parsing, allocation, copies and map locality: measurable secondary
   candidates.** At c100, perf flat samples attribute 6.77% to HttpParser::parse,
   3.82% to HttpResponse::serialize, 3.37% to malloc, 2.51% to free, and 1.62% to
   the request copy. At c5000, parser samples rise to 9.37% and connection-map
   lookup to 5.71%. These are sampled event shares, not absolute wall-time
   fractions or independently proven optimization gains. Allocation is visible,
   but there is no memory-pressure evidence: representative c5000 RSS is about
   13 MiB and major faults are zero. The timer scans once per second and does not
   appear among the reported >=0.5% self-sample hotspots.
4. **Shared metrics contention: plausible, lower evidence/priority.** All reactors
   update the same relaxed atomics. At c100, incrementRequests and recordLatency
   have 1.49% and 1.03% self samples respectively. Cache-line contention was not
   directly measured; do not infer a dominant lock bottleneck from these fields.
5. **Uneven connection distribution or worker mutexes: not supported as the
   cause of the /hello degradation.** At c5000, all 16 epolls have 285–337 client
   connections, totaling 5000. At nominal c100, counts are 3–10, totaling 96
   (wrk's per-thread rounding). The eight workers have zero measured CPU and
   steady-state context switches on /hello. strace's 46.94% futex time is mostly
   sleeping worker condition variables summed across threads; only 208 futex
   calls occur in the entire traced run. It is not 47% CPU spent contending.

perf sampled cycles:u at 199 Hz with frame-pointer call graphs at c100/c5000.
Kernel symbols were unavailable and the capture includes unresolved kernel/skid
samples, so it cannot precisely break down kernel function CPU cost. strace
substantially slows the server (about 34k responses/s during the baseline trace);
its count ratios are evidence, its summed elapsed times are not untraced CPU
percentages. Native perf captures remain locally available; text reports are
retained for review.

## Change and correctness

Only production files include/server/Reactor.hpp and src/server/Reactor.cpp
change. ConnectionState tracks the installed epoll mask. updateEvents skips
unchanged masks, updating the cache only after epoll_ctl succeeds. handleClient
and completeAsyncResponse immediately call handleWrite. A blocked write enables
EPOLLOUT; draining output restores EPOLLIN. An empty-output guard handles writable
events already present in a returned epoll batch without double-counting a flush.

The real-socket regression script tests repeated keep-alive, HEAD /hello,
fragmented input, synchronous pipelining, Connection: close, a paused reader
receiving a verified 6 MiB static file followed by another request, and async
/tasks callbacks followed by synchronous requests on the same connection.
It exercises unavailable Redis and, separately, successful seeded Redis.

## Remaining scope

The secondary LB matrix uses four LB reactors and one backend on the same server
CPU set. It measures proxy overhead, not three-backend distribution/failover.
The Redis matrix uses an isolated 100-task fixture and one 10-second sample per
level; its results are characterization, not a repeated confidence estimate.
At c5000, queue overload produces mostly HTTP errors: raw total response rate
must not be reported as successful task throughput, and mixed success/error
latency cannot be read as successful-request latency.

Static-file throughput, request bodies/chunked parsing, TLS (not implemented by
this HTTP server), connection churn, production arrival patterns and a remote
load generator are outside the primary matrix. No speculative parser, allocator,
metrics, Redis, LB, thread-count or architecture changes were made.

## Primary before/after measurements

Values are medians of three runs; each metric is aggregated independently.
CPU is server process CPU (100% = one logical CPU), excluding separately accounted
softirq work. CPU microseconds/request is therefore a process-cost measure, not
total network-stack CPU. All 24 primary runs have no reported socket or HTTP errors.

| Connections | Before req/s | After req/s | Change | Mean latency ms before → after | p99 ms before → after | CPU % before → after | CPU µs/request before → after |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 418,996 | 536,772 | +28.1% | 0.402 → 0.226 | 2.73 → 1.80 | 599.7 → 557.9 | 14.31 → 10.46 |
| 500 | 423,068 | 564,681 | +33.5% | 1.220 → 0.744 | 4.42 → 3.44 | 593.0 → 549.9 | 14.02 → 9.67 |
| 1000 | 412,136 | 568,379 | +37.9% | 2.300 → 1.300 | 6.24 → 4.20 | 584.5 → 546.4 | 14.12 → 9.68 |
| 5000 | 366,991 | 390,663 | +6.5% | 12.900 → 6.880 | 27.27 → 17.24 | 577.2 → 451.1 | 15.67 → 11.55 |

| Connections | Before req/s range | After req/s range |
|---:|---:|---:|
| 100 | 391,480–507,306 | 531,661–546,601 |
| 500 | 409,549–524,632 | 538,614–568,592 |
| 1000 | 401,176–508,780 | 548,656–568,680 |
| 5000 | 317,889–380,837 | 386,275–394,549 |

The baseline median peaks at c500, begins to drop at c1000, and is 13.3% below
that peak at c5000. The c500→c1000 difference is smaller than baseline variability;
the clearest degradation is c5000, where latency rises markedly. After the fix,
throughput peaks at c1000 and falls at c5000; client saturation prevents interpreting
that fall as a pure server limit. In after/r1-c5000.mpstat, client CPUs 8–15 have
only 0.53–0.67% idle while server CPUs retain 9.6–11.6% idle. The primary median
throughput gain at c5000 is only 6.5%, despite mean latency falling 46.7% and server
CPU/request falling about 26%. Use the later saved-binary recheck to assess drift.

| Separate traced c1000 run | Before | After |
|---|---:|---:|
| sendto calls | 170,903 | 268,809 |
| epoll_ctl calls | 343,781 | 1,677 |
| epoll_ctl / sendto | 2.012 | 0.00624 |
| recvfrom calls | 343,078 | 538,789 |
| epoll_wait calls | 5,568 | 4,414 |

That is a 99.69% reduction in epoll control calls per send. The receive/send path
still works; the removed work is the unconditional writable-interest round trip.
The traced throughput improvement is not used as the benchmark gain.

## Secondary workload results

One 10-second measurement per cell; these are not three-run medians.

| Path | Connections | Successful req/s before → after | Mean latency before → after | p99 before → after | Error fraction before → after | Backend CPU % before → after |
|---|---:|---:|---:|---:|---:|---:|
| lb | 100 | 183,160 → 183,182 | 536.50us → 535.02us | 1.71ms → 1.63ms | 0.00% → 0.00% | 266.4 → 235.3 |
| lb | 500 | 154,977 → 179,263 | 3.18ms → 2.76ms | 5.45ms → 3.71ms | 0.00% → 0.00% | 234.8 → 225.3 |
| lb | 1000 | 154,973 → 154,154 | 6.42ms → 6.45ms | 8.71ms → 9.01ms | 0.00% → 0.00% | 230.7 → 205.6 |
| lb | 5000 | 103,296 → 123,733 | 47.88ms → 40.07ms | 58.42ms → 51.03ms | 0.00% → 0.00% | 159.5 → 167.9 |
| tasks | 100 | 4,831 → 4,729 | 19.68ms → 20.25ms | 22.20ms → 23.14ms | 0.00% → 0.00% | 55.4 → 52.3 |
| tasks | 500 | 4,693 → 4,726 | 104.80ms → 104.10ms | 116.90ms → 114.05ms | 0.00% → 0.00% | 55.1 → 52.4 |
| tasks | 1000 | 4,609 → 4,582 | 213.65ms → 214.95ms | 271.39ms → 272.32ms | 0.00% → 0.00% | 54.6 → 52.8 |
| tasks | 5000 | 1,768 → 1,603 | 397.87ms → 426.34ms | 1.91s → 2.06s | 99.56% → 99.64% | 557.2 → 521.4 |

The LB /hello result is essentially unchanged at c100/c1000 and improves at
c500/c5000 in these single samples; the server change does not remove proxy
forwarding overhead. This is not enough evidence to rank specific LB functions
or claim a reliable percentage improvement for the production deployment.

/tasks shows no meaningful improvement at c100–c1000. At c5000, the bounded
4096-job queue rejects work: before, 4,018,260 of 4,036,017 responses are errors;
after, 4,520,435 of 4,536,623 are errors. Successful throughput is lower in the
after sample, not higher, even though rejection throughput increases. The faster
HTTP path is not a fix for Redis saturation, admission control or overload
fairness. No successful-response latency distribution is available from this
mixed-response wrk output.

## Saved-original-binary drift check

After the main and supplementary matrices, the retained original binary and then
the changed binary each ran exactly the same four concurrency levels, 3-second
warmups and 15-second measurements once. This is a second check, not an additional
independent three-run median.

| Connections | Original req/s | Changed req/s | Change | Mean latency before → after | p99 before → after | CPU % before → after | CPU µs/request before → after |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 484,479 | 522,222 | +7.8% | 377.76us → 224.19us | 2.63ms → 1.75ms | 603.0 → 560.6 | 12.45 → 10.73 |
| 500 | 511,187 | 550,751 | +7.7% | 1.05ms → 735.09us | 4.30ms → 3.20ms | 600.7 → 554.2 | 11.75 → 10.06 |
| 1000 | 480,011 | 550,497 | +14.7% | 2.00ms → 1.24ms | 5.78ms → 3.58ms | 595.9 → 554.5 | 12.41 → 10.07 |
| 5000 | 362,631 | 386,449 | +6.6% | 13.31ms → 6.56ms | 24.64ms → 13.58ms | 591.8 → 446.3 | 16.32 → 11.55 |

The recheck supports the direction of the change but moderates the magnitude:
throughput gains are 7.8%, 7.7%, 14.7%, and 6.6%, rather than taking the early
28–38% low/mid-concurrency median improvements as entirely causal. Mean and p99
latency and CPU per response improve at all levels. At c5000, mean latency falls
50.7%, p99 falls 44.9%, and process CPU/request falls 29.2%. There are no reported
HTTP or socket errors in these eight validation runs. A remote, unsaturated load
generator and interleaved repeated trials on an isolated host are needed for a
precise capacity claim; no such claim is made here.

## Redis attribution and final correctness evidence

A separate unchanged-server /tasks c500 profile with the same 100-task fixture
returned 4,656.63 successful responses/s. Redis INFO counters show approximately
9.858 CPU seconds during the 10.04-second load, close to one fully utilized core.
EVAL counters increase by 46,973 calls and 9,480,712 microseconds, or about
201.8 microseconds per call. This alone implies a roughly 4,955 calls/s execution
ceiling before surrounding I/O overhead, consistent with observed throughput.
RedisClient::getAllTasks evaluates a Lua script that runs SMEMBERS and HGETALL
for every task, then returns the complete listing. This is direct runtime
evidence of the separate storage/execution limit, rather than a guess that the
worker mutex is responsible. Relevant downstream work is TaskManager::getAll
(copy/sort) and WorkerPool::submit/workerLoop/publishCompletion. Redis's INFO
instantaneous_ops_per_sec includes the commands executed inside the Lua script;
it must not be treated as HTTP request throughput.

The final traced socket test records sendfile returning EAGAIN four times for
the paused-reader fixture, EPOLLOUT being enabled, and EPOLLOUT being removed
after the queue drains. The full 6 MiB content hash and the following keep-alive
response both match. Async callback tests pass both with unavailable Redis (503)
and with the healthy seeded fixture (200). Build and git diff --check pass.
All benchmark servers and disposable Redis containers were stopped; the original
configuration and public directory contents were preserved.

Review artifacts:

* [Primary raw before results](results/before/) and [after results](results/after/).
* [Saved-original recheck](results/before-validation/) and [changed recheck](results/after-validation/).
* [Baseline syscall counts](results/profile-before/strace-summary.txt) and [changed syscall counts](results/profile-after/strace-summary.txt).
* [Baseline c5000 profile](results/profile-before/c5000.report) and [changed c5000 profile](results/profile-after/c5000.report).
* [Redis attribution](results/tasks-profile/) and [backpressure syscall trace](results/correctness-syscalls.txt).
* [Socket regression test](../tests/reactor_output.py), [benchmark harness](run.py), and [summary utility](summarize.py).
