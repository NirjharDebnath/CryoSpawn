#!/bin/bash
# Find and delete all interfaces starting with "cryo"

for interface in $(ip -o link show | awk -F': ' '{print $2}' | grep '^cryo'); do
    echo "Deleting interface: $interface"
    sudo ip link set dev "$interface" down
    sudo ip link delete "$interface" type tun
done