#!/usr/bin/env bash
#
# switch-gdb-pid.sh - find the PID of a running homebrew process on the Switch
# via Atmosphere's built-in GDB server (dmnt.gen2), so you know what to pass
# to "attach <pid>" in gdb / VSCode.
#
# Usage:
#   switch-gdb-pid.sh <switch-ip> [process-name]
#
# Examples:
#   switch-gdb-pid.sh 192.168.31.132            # list all processes
#   switch-gdb-pid.sh 192.168.31.132 pfbneo     # print only pfbneo's PID
#
# Requires: aarch64-none-elf-gdb (devkitA64) and enable_standalone_gdbstub=1
# in atmosphere/config/system_settings.ini on the Switch (default TCP port 22225).
set -euo pipefail

GDB="${AARCH64_GDB:-/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb}"
PORT="${PEMU_SWITCH_PORT:-22225}"

IP="${1:?usage: $0 <switch-ip> [process-name]}"
NAME="${2:-}"

if [ -n "$NAME" ]; then
  PID="$("$GDB" -q -batch -nx \
    -ex "target extended-remote ${IP}:${PORT}" \
    -ex "info os processes" \
    2>/dev/null | awk -v n="$NAME" '$2 == n {print $1; exit}')"
  if [ -z "$PID" ]; then
    echo "error: no process named '$NAME' found on ${IP}" >&2
    exit 1
  fi
  echo "$PID"
else
  "$GDB" -q -batch -nx \
    -ex "target extended-remote ${IP}:${PORT}" \
    -ex "info os processes"
fi