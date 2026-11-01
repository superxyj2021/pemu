#!/usr/bin/env bash
#
# switch-nxlink.sh - build (optional) and push/run a pfbneo NRO on the Switch
# via nxlink (devkitPro). USB by default, wireless with -a.
#
# Usage:
#   switch-nxlink.sh [options]
#
# Options:
#   -r | --release      use the Release NRO (default: Debug)
#   -b | --build        (re)build the NRO before pushing
#   -a | --address IP   push over the network to IP (default: USB, or PEMU_SWITCH_IP)
#   -p | --path REMOTE  upload path on the switch (default: pfbneo/pfbneo.nro)
#   -h | --help         show this help
#
# Examples:
#   scripts/switch-nxlink.sh                       # USB push Debug NRO, attach stdout
#   scripts/switch-nxlink.sh -b                    # build Debug, then USB push
#   scripts/switch-nxlink.sh -r -b -a              # build Release, push over wifi
#
# Env overrides: PEMU_SWITCH_IP, PEMU_BUILD_ROOT, PEMU_BUILD_MODE, PEMU_NRO, NXLINK
#
set -euo pipefail

NXLINK="${NXLINK:-/opt/devkitpro/tools/bin/nxlink}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PEMU_BUILD_ROOT:-$(dirname "$SCRIPT_DIR")}"
IP="${PEMU_SWITCH_IP:-192.168.31.132}"
MODE="${PEMU_BUILD_MODE:-debug}"
REMOTE_PATH="pfbneo/pfbneo.nro"
DO_BUILD=0
ADDRESS=""

usage() {
    awk 'NR == 1 {next} /^#/ {l = $0; sub(/^# ?/, "", l); if (length(l)) print l; next} {exit}' "${BASH_SOURCE[0]}"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -r|--release) MODE="release" ;;
        -b|--build) DO_BUILD=1 ;;
        -a|--address) ADDRESS="${2:?missing address}"; shift ;;
        -p|--path) REMOTE_PATH="${2:?missing path}"; shift ;;
        -h|--help) usage ;;
        *) echo "error: unknown option: $1" >&2; usage 1 ;;
    esac
    shift
done

BUILD_DIR="${PROJECT_ROOT}/cmake-build-switch-${MODE}"
NRO="${PEMU_NRO:-${BUILD_DIR}/src/cores/pfbneo/pfbneo.nro}"

if [ "$DO_BUILD" -eq 1 ]; then
    if [ ! -d "$BUILD_DIR" ]; then
        echo "error: build dir not found: $BUILD_DIR (configure it first)" >&2
        exit 1
    fi
    echo "==> building pfbneo (${MODE})..."
    make -C "$BUILD_DIR" pfbneo.deps >/dev/null
    make -C "$BUILD_DIR" pfbneo.nro
fi

if [ ! -f "$NRO" ]; then
    echo "error: NRO not found: $NRO (build it first, or use -b)" >&2
    exit 1
fi

CMD=("$NXLINK" "-p" "$REMOTE_PATH")
if [ -n "$ADDRESS" ]; then
    CMD+=("-a" "$ADDRESS")
fi
CMD+=("-s" "$NRO")

echo "==> pushing ${NRO} to the Switch..."
echo "    $(printf '%q ' "${CMD[@]}")"
exec "${CMD[@]}"