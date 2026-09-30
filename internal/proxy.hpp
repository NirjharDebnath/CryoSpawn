#pragma once
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <functional>

// Forward declare the callbacks we need
using WakeCallback = std::function<bool(int)>;
using StatusCallback = std::function<bool(int)>;

inline void relay_data(int src_fd, int dst_fd) {
    char buffer[8192];
    while (true) {
        ssize_t bytes = recv(src_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break; // Connection closed or error
        send(dst_fd, buffer, bytes, 0);
    }
    shutdown(src_fd, SHUT_RDWR);
    shutdown(dst_fd, SHUT_RDWR);
    close(src_fd);
    close(dst_fd);
}

inline void handle_proxy_client(int client_sock, std::string vm_ip, int vm_port, int vm_id, WakeCallback wake_fn, StatusCallback status_fn) {
    // 1. Is the VM asleep? If so, be the doorman and wake it up!
    if (status_fn(vm_id)) {
        std::cout << "[Ghost Proxy] Traffic detected! Waking VM " << vm_id << " from hibernation...\n";
        wake_fn(vm_id);
        // Give the Firecracker microVM 600ms to boot its networking stack
        std::this_thread::sleep_for(std::chrono::milliseconds(600)); 
    }

    // 2. Connect to the internal VM
    int vm_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in vm_addr;
    vm_addr.sin_family = AF_INET;
    vm_addr.sin_port = htons(vm_port);
    inet_pton(AF_INET, vm_ip.c_str(), &vm_addr.sin_addr);

    // Try connecting with retries (in case the VM is still setting up its IP)
    bool connected = false;
    for(int i = 0; i < 50; i++) {
        if (connect(vm_sock, (struct sockaddr*)&vm_addr, sizeof(vm_addr)) == 0) {
            connected = true; 
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!connected) {
        std::cerr << "[Ghost Proxy] Failed to route to VM " << vm_id << " on port " << vm_port << " (Timeout)\n";
        close(client_sock);
        close(vm_sock);
        return;
    }

    std::cout << "[Ghost Proxy] Successfully connected to VM " << vm_id << "! Relaying data...\n";

    // 3. Setup bidirectional relay threads (Wire them together)
    std::thread t1(relay_data, client_sock, vm_sock);
    std::thread t2(relay_data, vm_sock, client_sock);
    t1.detach();
    t2.detach();
}

#include <sys/un.h>

inline void run_ghost_proxy(const std::string& uds_path, std::string vm_ip, int vm_port, int vm_id, WakeCallback wake_fn, StatusCallback status_fn) {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[-] Ghost Proxy failed to create Unix socket for VM " << vm_id << "\n";
        return;
    }
    
    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, uds_path.c_str(), sizeof(address.sun_path) - 1);

    // Ensure stale socket files are removed before binding
    unlink(uds_path.c_str());

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Ghost Proxy bind failed on " << uds_path << "\n";
        close(server_fd);
        return;
    }

    // Allow nginx (or any user) to write to this socket
    chmod(uds_path.c_str(), 0666);

    listen(server_fd, 10);
    std::cout << "[+] Ghost Proxy listening on " << uds_path << " -> forwarding to VM " << vm_id << " (" << vm_ip << ":" << vm_port << ")\n";

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        if (client_sock >= 0) {
            // Spawn a handler thread for this user
            std::thread(handle_proxy_client, client_sock, vm_ip, vm_port, vm_id, wake_fn, status_fn).detach();
        }
    }
}
