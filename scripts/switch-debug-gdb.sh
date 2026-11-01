#!/usr/bin/env bash
#
# switch-debug-gdb.sh - attach gdb to pfbneo running on the Switch
# (Atmosphere built-in GDB server via dmnt.gen2).
#
# Usage:
#   switch-debug-gdb.sh [switch-ip] [hbloader-pid]
#
# With no pid it auto-detects the hbloader process via switch-gdb-pid.sh.
# Once in gdb:
#   break ui_main.cpp:55   -> set breakpoint
#   continue               -> run
#   bt / p this / n / s    -> inspect / step
#
set -euo pipefail

GDB="${AARCH64_GDB:-/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb}"
ELF="${PEMU_ELF:-/home/superxyj/pemu/cmake-build-switch-debug/src/cores/pfbneo/pfbneo}"
PORT="${PEMU_SWITCH_PORT:-22225}"
IP="${1:-192.168.31.132}"
PID="${2:-}"

if [ ! -f "$ELF" ]; then
    echo "error: ELF not found: $ELF (build Debug first)" >&2
    exit 1
fi

if [ -z "$PID" ]; then
    PID="$(PEMU_SWITCH_PORT="$PORT" "$(dirname "$0")/switch-gdb-pid.sh" "$IP" hbloader)"
    echo "auto-detected hbloader pid: $PID"
fi

exec "$GDB" -q "$ELF" \
    -ex "set pagination off" \
    -ex "set solib-search-path $(dirname "$ELF")" \
    -ex "target extended-remote ${IP}:${PORT}" \
    -ex "attach ${PID}" \
    -ex "file" \
    -ex "sharedlibrary"