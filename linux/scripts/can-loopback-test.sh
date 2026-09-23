#!/bin/sh
# CAN loopback test for the MCP2515 on PmodCAN #1.
#
# The controller is put in its internal loopback mode, so a frame it sends is received by
# itself: no bus, no termination and no second node are needed. Requires can-utils and iproute2
# (both are in satlink-image).
#
#   can-loopback-test.sh [iface]     default: can0; BITRATE overrides 500000
#
# @verifies SRS-PER-002
set -eu

iface="${1:-can0}"
bitrate="${BITRATE:-500000}"
log="$(mktemp)"
dump_pid=""

cleanup() {
    if [ -n "$dump_pid" ]; then
        kill "$dump_pid" 2>/dev/null || true
    fi
    ip link set "$iface" down 2>/dev/null || true
    rm -f "$log"
}
trap cleanup EXIT

ip link set "$iface" down 2>/dev/null || true
ip link set "$iface" type can bitrate "$bitrate" loopback on
ip link set "$iface" up

candump "$iface" > "$log" &
dump_pid=$!
sleep 1

cansend "$iface" 123#DEADBEEF
sleep 1

if grep -qi "123 .*DE AD BE EF" "$log"; then
    echo "PASS: frame 123#DEADBEEF looped back on $iface"
else
    echo "FAIL: no looped-back frame on $iface" >&2
    cat "$log" >&2
    exit 1
fi
