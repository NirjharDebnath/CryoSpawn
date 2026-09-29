#!/bin/bash
sudo killall firecracker cryospawn 2>/dev/null
sleep 1
sudo ./internal/cryospawn &
DAEMON_PID=$!
sleep 2

# Create VM
echo "[*] Creating VM..."
curl -s -X POST http://localhost:8080/api/vms -H "Content-Type: application/json" -d '{"vcpus": 1, "mem_mib": 512, "expose_ssh": true, "expose_http": true}' > /dev/null

sleep 5

# Hibernate VM
echo "[*] Hibernating VM..."
curl -s -X POST http://localhost:8080/api/vms/0/hibernate > /dev/null

sleep 2

# Wake VM
echo "[*] Waking VM..."
curl -s -X POST http://localhost:8080/api/vms/0/wake > /dev/null

sleep 2
echo "[*] Pinging VM..."
ping -c 1 -W 2 172.16.0.2

sleep 2
echo "[*] VM Status:"
curl -s http://localhost:8080/api/vms | grep -o '"status":"[^"]*"'

sudo kill $DAEMON_PID
