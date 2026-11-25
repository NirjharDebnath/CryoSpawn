#pragma once
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
#include <atomic>

// Forward declare callbacks
using WakeCallback = std::function<bool(int)>;
using StatusCallback = std::function<bool(int)>;

inline std::mutex proxy_registry_mutex;
inline std::map<std::string, int> active_proxies;
inline std::atomic<int> active_proxy_connections{0};
constexpr int MAX_PROXY_CONNECTIONS = 128;

struct ProxyConnGuard {
    ProxyConnGuard() { active_proxy_connections.fetch_add(1, std::memory_order_relaxed); }
    ~ProxyConnGuard() { active_proxy_connections.fetch_sub(1, std::memory_order_relaxed); }
};

inline void relay_half(int src_fd, int dst_fd) {
    char buffer[16384];
    while (true) {
        ssize_t bytes = recv(src_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break; // Connection closed or error
        
        ssize_t total_sent = 0;
        while (total_sent < bytes) {
            ssize_t s = send(dst_fd, buffer + total_sent, bytes - total_sent, MSG_NOSIGNAL);
            if (s <= 0) goto end;
            total_sent += s;
        }
    }
end:
    // Half-close write side of destination to signal clean EOF to peer
    shutdown(dst_fd, SHUT_WR);
    shutdown(src_fd, SHUT_RD);
}

inline void handle_proxy_client(int client_sock, std::string vm_ip, int vm_port, int vm_id, WakeCallback wake_fn, StatusCallback status_fn) {
    ProxyConnGuard conn_guard;
    // 1. Is the VM hibernated or stopped? If so, wake or boot it!
    if (status_fn(vm_id)) {
        std::cout << "[Ghost Proxy] Traffic detected! Waking/starting VM " << vm_id << "...\n";
        wake_fn(vm_id);
        // Give the microVM time to initialize its network interface
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }

    // 2. Connect to the internal VM
    struct sockaddr_in vm_addr;
    std::memset(&vm_addr, 0, sizeof(vm_addr));
    vm_addr.sin_family = AF_INET;
    vm_addr.sin_port = htons(vm_port);
    inet_pton(AF_INET, vm_ip.c_str(), &vm_addr.sin_addr);

    int vm_sock = -1;
    bool connected = false;
    for (int i = 0; i < 50; i++) {
        vm_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (vm_sock < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (connect(vm_sock, (struct sockaddr*)&vm_addr, sizeof(vm_addr)) == 0) {
            connected = true; 
            break;
        }
        close(vm_sock);
        vm_sock = -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!connected || vm_sock < 0) {
        std::cerr << "[Ghost Proxy] Failed to route to VM " << vm_id << " on port " << vm_port << " (Timeout)\n";
        close(client_sock);
        if (vm_sock >= 0) close(vm_sock);
        return;
    }

    std::cout << "[Ghost Proxy] Connected to VM " << vm_id << " (" << vm_ip << ":" << vm_port << "). Relaying data...\n";

    // 3. Bidirectional relay with clean half-close and coordinated single close
    std::thread t([client_sock, vm_sock]() {
        relay_half(client_sock, vm_sock);
    });
    relay_half(vm_sock, client_sock);
    t.join();

    close(client_sock);
    close(vm_sock);
}

inline bool is_ghost_proxy_running(const std::string& key) {
    std::lock_guard<std::mutex> lock(proxy_registry_mutex);
    return active_proxies.find(key) != active_proxies.end();
}

inline void stop_ghost_proxy(const std::string& key) {
    std::lock_guard<std::mutex> lock(proxy_registry_mutex);
    auto it = active_proxies.find(key);
    if (it != active_proxies.end()) {
        int fd = it->second;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        active_proxies.erase(it);
        if (key.rfind("/tmp/", 0) == 0) {
            unlink(key.c_str());
        }
        std::cout << "[*] Stopped Ghost Proxy on " << key << "\n";
    }
}

inline void stop_all_ghost_proxies_for_vm(const std::string& hex, int slot_id = -1, int host_port = 0) {
    std::vector<std::string> keys_to_stop;
    std::string prefix = "/tmp/cryo-" + hex + "_";

    {
        std::lock_guard<std::mutex> lock(proxy_registry_mutex);
        for (const auto& [k, v] : active_proxies) {
            if (k.rfind(prefix, 0) == 0) {
                keys_to_stop.push_back(k);
            }
        }
    }

    for (const auto& k : keys_to_stop) {
        stop_ghost_proxy(k);
    }

    if (slot_id >= 0) {
        unlink(("/tmp/cryo_vm_" + std::to_string(slot_id) + "_22.sock").c_str());
        unlink(("/tmp/cryo_vm_" + std::to_string(slot_id) + "_80.sock").c_str());
        unlink(("/tmp/cryo_vm_" + std::to_string(slot_id) + "_443.sock").c_str());
    }

    if (host_port > 0) {
        stop_ghost_proxy("tcp:" + std::to_string(host_port));
    }
}

// Spawns a Unix Domain Socket Ghost Proxy
inline void run_ghost_proxy(const std::string& uds_path, std::string vm_ip, int vm_port, int vm_id, WakeCallback wake_fn, StatusCallback status_fn, int slot_id = -1) {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[-] Ghost Proxy failed to create Unix socket: " << uds_path << "\n";
        return;
    }
    
    struct sockaddr_un address;
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, uds_path.c_str(), sizeof(address.sun_path) - 1);

    unlink(uds_path.c_str());

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Ghost Proxy bind failed on " << uds_path << "\n";
        close(server_fd);
        return;
    }

    chmod(uds_path.c_str(), 0666);

    // Create legacy symlink for nginx/client backwards compatibility (e.g. vm0.cryo -> cryo_vm_0_80.sock)
    if (slot_id >= 0) {
        std::string legacy_sock = "/tmp/cryo_vm_" + std::to_string(slot_id) + "_" + std::to_string(vm_port) + ".sock";
        unlink(legacy_sock.c_str());
        symlink(uds_path.c_str(), legacy_sock.c_str());
    }

    listen(server_fd, 20);
    {
        std::lock_guard<std::mutex> lock(proxy_registry_mutex);
        active_proxies[uds_path] = server_fd;
    }
    std::cout << "[+] Ghost Proxy (UDS) listening on " << uds_path << " -> " << vm_ip << ":" << vm_port << "\n";

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        if (client_sock < 0) {
            break; // Socket was closed, terminate listener thread
        }
        if (active_proxy_connections.load(std::memory_order_relaxed) >= MAX_PROXY_CONNECTIONS) {
            close(client_sock);
            continue;
        }
        std::thread(handle_proxy_client, client_sock, vm_ip, vm_port, vm_id, wake_fn, status_fn).detach();
    }
}

// Spawns a native TCP Host Port Ghost Proxy
inline void run_ghost_proxy_tcp(int host_port, std::string vm_ip, int vm_port, int vm_id, WakeCallback wake_fn, StatusCallback status_fn) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[-] Ghost Proxy failed to create TCP socket for port " << host_port << "\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(host_port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Ghost Proxy TCP bind failed on port " << host_port << "\n";
        close(server_fd);
        return;
    }

    listen(server_fd, 20);
    std::string key = "tcp:" + std::to_string(host_port);
    {
        std::lock_guard<std::mutex> lock(proxy_registry_mutex);
        active_proxies[key] = server_fd;
    }
    std::cout << "[+] Ghost Proxy (TCP) listening on 0.0.0.0:" << host_port << " -> " << vm_ip << ":" << vm_port << "\n";

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        if (client_sock < 0) {
            break;
        }
        if (active_proxy_connections.load(std::memory_order_relaxed) >= MAX_PROXY_CONNECTIONS) {
            close(client_sock);
            continue;
        }
        std::thread(handle_proxy_client, client_sock, vm_ip, vm_port, vm_id, wake_fn, status_fn).detach();
    }
}