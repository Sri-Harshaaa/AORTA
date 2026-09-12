# AORTA performance investigation

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
