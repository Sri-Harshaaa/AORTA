#!/usr/bin/env bash
#
# Host tuning for high-concurrency runs.
#
# Without this the benchmark measures kernel defaults rather than AORTA. Every
# value here is printed before and after so a run can be reproduced, and so the
# write-up can state what the host was actually configured to do.
#
# Needs root. Changes are not persistent across reboot by design.

set -euo pipefail

if [[ "${1:-}" == "--show" ]]; then
    SHOW_ONLY=1
else
    SHOW_ONLY=0
fi

show() {
    echo "current settings"
    echo "  ulimit -n (soft)          $(ulimit -Sn)"
    echo "  ulimit -n (hard)          $(ulimit -Hn)"
    for key in \
        fs.file-max \
        net.core.somaxconn \
        net.ipv4.tcp_max_syn_backlog \
        net.ipv4.ip_local_port_range \
        net.ipv4.tcp_tw_reuse \
        net.ipv4.tcp_fin_timeout \
        net.core.netdev_max_backlog
    do
        printf '  %-25s %s\n' "$key" "$(sysctl -n "$key" 2>/dev/null || echo '(unavailable)')"
    done
}

show

if [[ $SHOW_ONLY -eq 1 ]]; then
    exit 0
fi

if [[ "$(id -u)" -ne 0 ]]; then
    echo
    echo "Re-run with sudo to apply. Use --show to only inspect." >&2
    exit 1
fi

echo
echo "applying"

# Descriptors. Each connection costs one on the server and one on the client,
# and the balancer costs two because it holds both sides.
sysctl -w fs.file-max=2097152

# Accept queue. The listen() backlog is clamped to this, so raising the backlog
# in code without raising this achieves nothing.
sysctl -w net.core.somaxconn=65535
sysctl -w net.ipv4.tcp_max_syn_backlog=65535
sysctl -w net.core.netdev_max_backlog=65535

# Ephemeral ports. A single client host can hold at most one connection per
# (source port, destination) pair, so this range is a hard ceiling of roughly
# 64000 connections to one destination - worth knowing before blaming the
# server for a wall near 64k.
sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# Let closed sockets be reused rather than sitting in TIME_WAIT for a minute,
# which otherwise exhausts the port range within a few benchmark steps.
sysctl -w net.ipv4.tcp_tw_reuse=1
sysctl -w net.ipv4.tcp_fin_timeout=15

echo
echo "raise the descriptor limit in the shell that runs the benchmark:"
echo "  ulimit -n 1048576"
echo
show
