#!/usr/bin/env bash
#
# Renders two benchmark CSVs side by side, so the thread-versus-epoll result
# can be read without a spreadsheet.
#
#   ./bench/compare.sh bench/results/epoll.csv bench/results/threaded.csv

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: compare.sh <a.csv> <b.csv>" >&2
    exit 1
fi

A="$1"
B="$2"

for file in "$A" "$B"; do
    if [[ ! -f "$file" ]]; then
        echo "no such file: $file" >&2
        exit 1
    fi
done

name_a="$(basename "$A" .csv)"
name_b="$(basename "$B" .csv)"

printf '%-8s | %-24s | %-24s\n' "" "$name_a" "$name_b"
printf '%-8s | %10s %6s %6s | %10s %6s %6s\n' \
    "conns" "rps" "p99ms" "rssMB" "rps" "p99ms" "rssMB"
printf '%s\n' "---------+--------------------------+-------------------------"

join -t, -1 1 -2 1 \
    <(tail -n +2 "$A" | sort -t, -k1,1) \
    <(tail -n +2 "$B" | sort -t, -k1,1) \
    2>/dev/null \
| sort -t, -k1,1n \
| awk -F, '{
    printf "%-8s | %10s %6s %6s | %10s %6s %6s\n",
        $1, $3, $7, $11, $15, $19, $23
}'

echo
echo "Columns: rps = requests/sec sustained, p99ms = 99th percentile latency,"
echo "rssMB = server resident memory. A row missing from one file means that"
echo "model could not complete that step."
