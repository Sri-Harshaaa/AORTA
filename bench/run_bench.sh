#!/usr/bin/env bash
#
# AORTA concurrency benchmark.
#
# Sweeps a connection progression against one target and writes a CSV row per
# step. Run it once per model and compare the files:
#
#   ./bench/run_bench.sh --label epoll    --url http://127.0.0.1:9000/hello
#   ./bench/run_bench.sh --label threaded --url http://127.0.0.1:9000/hello
#
# Uses wrk2 rather than wrk or ab. wrk2 holds a constant request rate and
# corrects for coordinated omission: a plain wrk run stops issuing requests
# while the server is stalled, so the stall never appears in the latency
# distribution and tail numbers come out far better than reality. Tail latency
# is the entire point of the thread-versus-epoll comparison, so measuring it
# with a tool that hides stalls would make the result meaningless.

set -uo pipefail

URL="http://127.0.0.1:9000/hello"
LABEL="run"
DURATION="30s"
THREADS="$(nproc 2>/dev/null || echo 4)"
RATE="20000"
CONNECTIONS="100 500 1000 5000 10000 50000"
OUT_DIR="bench/results"
WARMUP="5s"

usage() {
    cat <<'USAGE'
Usage: run_bench.sh [options]

  --url <url>           Target URL (default http://127.0.0.1:9000/hello)
  --label <name>        Name for this run; becomes the CSV filename
  --duration <time>     Seconds per step, wrk2 syntax (default 30s)
  --rate <n>            Requests/sec offered per step (default 20000)
  --threads <n>         wrk2 worker threads (default: nproc)
  --connections "a b c" Connection progression to sweep
  --out <dir>           Output directory (default bench/results)
  -h, --help            This message

The progression stops early if a step cannot open its connections, and the
CSV records that step as the observed ceiling rather than pretending the
target was met.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --url)         URL="$2"; shift 2 ;;
        --label)       LABEL="$2"; shift 2 ;;
        --duration)    DURATION="$2"; shift 2 ;;
        --rate)        RATE="$2"; shift 2 ;;
        --threads)     THREADS="$2"; shift 2 ;;
        --connections) CONNECTIONS="$2"; shift 2 ;;
        --out)         OUT_DIR="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if ! command -v wrk >/dev/null 2>&1; then
    cat >&2 <<'MISSING'
wrk2 is not installed.

    git clone https://github.com/giltene/wrk2 && cd wrk2 && make
    sudo cp wrk /usr/local/bin/wrk

The binary is called "wrk"; check `wrk --version` mentions wrk2 before
trusting any latency number this script produces.
MISSING
    exit 1
fi

mkdir -p "$OUT_DIR"

CSV="${OUT_DIR}/${LABEL}.csv"
LOG="${OUT_DIR}/${LABEL}.log"

METRICS_URL="$(echo "$URL" | sed -E 's#(https?://[^/]+).*#\1#')/metrics"

echo "connections,rate_target,rps,latency_avg_ms,p50_ms,p95_ms,p99_ms,p999_ms,\
socket_errors,non_2xx,rss_mb,cpu_percent,open_fds" > "$CSV"

: > "$LOG"

echo "AORTA benchmark"
echo "  target      $URL"
echo "  label       $LABEL"
echo "  duration    $DURATION per step at $RATE req/s"
echo "  output      $CSV"
echo

ulimit -n 2>/dev/null | {
    read -r limit
    echo "  ulimit -n   $limit"
    if [[ "$limit" != "unlimited" && "$limit" -lt 100000 ]]; then
        echo "  NOTE: raise it with 'ulimit -n 1048576' or steps above"
        echo "        ${limit} connections will fail on the client side."
    fi
}
echo

# Extract "Latency Distribution" percentiles from wrk2 output, in ms.
percentile() {
    local file="$1" want="$2"
    awk -v want="$want" '
        $1 == want {
            v = $2
            if (v ~ /us$/)      { sub(/us$/, "", v); printf "%.3f", v / 1000 }
            else if (v ~ /ms$/) { sub(/ms$/, "", v); printf "%.3f", v }
            else if (v ~ /s$/)  { sub(/s$/,  "", v); printf "%.3f", v * 1000 }
            else                { printf "%s", v }
            exit
        }
    ' "$file"
}

scrape() {
    local field="$1"
    curl -s --max-time 3 "$METRICS_URL" 2>/dev/null \
        | awk -v f="$field" '$1 == f { print $2; exit }'
}

echo "warming up for $WARMUP"
wrk -t2 -c50 -d"$WARMUP" -R1000 "$URL" >/dev/null 2>&1 || true

for connections in $CONNECTIONS; do

    threads="$THREADS"
    if (( connections < threads )); then
        threads="$connections"
    fi

    echo "--- ${connections} connections ---"

    step_out="$(mktemp)"

    wrk -t"$threads" \
        -c"$connections" \
        -d"$DURATION" \
        -R"$RATE" \
        --latency \
        "$URL" > "$step_out" 2>&1

    status=$?

    {
        echo "=== ${connections} connections ==="
        cat "$step_out"
        echo
    } >> "$LOG"

    if [[ $status -ne 0 ]] || ! grep -q "Requests/sec" "$step_out"; then
        echo "  step failed - recording as the observed ceiling"
        echo "${connections},${RATE},FAILED,,,,,,,,,," >> "$CSV"
        cat "$step_out" | tail -5
        rm -f "$step_out"
        break
    fi

    rps=$(awk '/Requests\/sec/ { print $2; exit }' "$step_out")
    avg=$(awk '/^ +Latency/ { print $2; exit }' "$step_out" \
          | sed -E 's/us$//; s/ms$//; s/s$//')

    # Normalise the average the same way as the percentiles.
    avg_unit=$(awk '/^ +Latency/ { print $2; exit }' "$step_out" \
               | grep -oE '(us|ms|s)$')
    case "$avg_unit" in
        us) avg=$(awk -v v="$avg" 'BEGIN { printf "%.3f", v / 1000 }') ;;
        s)  avg=$(awk -v v="$avg" 'BEGIN { printf "%.3f", v * 1000 }') ;;
    esac

    p50=$(percentile "$step_out" "50.000%")
    p95=$(percentile "$step_out" "95.000%")
    p99=$(percentile "$step_out" "99.000%")
    p999=$(percentile "$step_out" "99.900%")

    errors=$(awk '/Socket errors/ { print $0; exit }' "$step_out" \
             | grep -oE '[0-9]+' | paste -sd'+' - | bc 2>/dev/null || echo 0)
    errors=${errors:-0}

    non2xx=$(awk '/Non-2xx/ { print $NF; exit }' "$step_out")
    non2xx=${non2xx:-0}

    rss_bytes=$(scrape aorta_process_resident_memory_bytes)
    rss_mb=$(awk -v b="${rss_bytes:-0}" 'BEGIN { printf "%.1f", b / 1048576 }')
    cpu=$(scrape aorta_process_cpu_percent)
    fds=$(scrape aorta_process_open_fds)

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$connections" "$RATE" "$rps" "$avg" \
        "$p50" "$p95" "$p99" "$p999" \
        "$errors" "$non2xx" "$rss_mb" "${cpu:-0}" "${fds:-0}" >> "$CSV"

    echo "  rps=${rps}  p50=${p50}ms  p99=${p99}ms  rss=${rss_mb}MB  fds=${fds:-?}"

    rm -f "$step_out"

    # Let TIME_WAIT sockets drain so the next step starts from a clean slate.
    sleep 5
done

echo
echo "wrote $CSV"
echo "full wrk2 output in $LOG"
