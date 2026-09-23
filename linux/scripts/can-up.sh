#!/bin/sh
# Bring a SocketCAN interface up in normal mode.
#
#   can-up.sh [iface] [bitrate]      defaults: can0, 500000
#
# @implements SRS-PER-002
set -eu

iface="${1:-can0}"
bitrate="${2:-500000}"

ip link set "$iface" down 2>/dev/null || true
ip link set "$iface" type can bitrate "$bitrate"
ip link set "$iface" up
ip -details link show "$iface"
