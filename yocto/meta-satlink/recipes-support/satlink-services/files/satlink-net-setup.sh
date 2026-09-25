#!/bin/sh
# satlink-net-setup: address the two IP endpoints of the payload manager so that traffic
# between them crosses the modem link.
#
#   satlink-sat  10.77.0.1/30  stays in the main namespace ("the satellite")
#   satlink-gnd  10.77.0.2/30  is moved into the network namespace "ground"
#
# Without the namespace the kernel would deliver 10.77.0.1 -> 10.77.0.2 locally. Try:
#   ip netns exec ground ping 10.77.0.1        # every ICMP packet goes through the modem
#   ip netns exec ground iperf3 -c 10.77.0.1   # link throughput at the current MODCOD
#
# @implements SRS-NET-001
set -eu

case "${1:-up}" in
up)
    ip netns add ground 2>/dev/null || true
    ip link set satlink-gnd netns ground
    ip addr replace 10.77.0.1/30 dev satlink-sat
    ip link set satlink-sat mtu 1000 up
    ip netns exec ground ip addr replace 10.77.0.2/30 dev satlink-gnd
    ip netns exec ground ip link set satlink-gnd mtu 1000 up
    ip netns exec ground ip link set lo up
    ;;
down)
    ip netns del ground 2>/dev/null || true
    ;;
*)
    echo "usage: satlink-net-setup up|down" >&2
    exit 2
    ;;
esac
