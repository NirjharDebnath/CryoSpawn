#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

bool is_http_success(const std::string& response) {
    // Check if the response begins with HTTP/1.1 or HTTP/1.0
    if (response.rfind("HTTP/1.", 0) == 0 && response.length() >= 12) {
        std::string code_str = response.substr(9, 3);
        try {
            int code = std::stoi(code_str);
            return (code >= 200 && code < 300);
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool send_firecracker_put (const std::string& socket_path, const std::string& endpoint, std::string& json_body) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { 
        std::cerr << "[-] Failed to create Unix socket.\n";
        return false;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[-] Failed to connect to socket: " << socket_path << " (" << strerror(errno) << ")\n";
        close(sock);
        return false;
    }

    // Build standard HTTP 1.1 PUT request
    std::string request = 
        "PUT " + endpoint + " HTTP/1.1\r\n" +
        "Host: localhost\r\n" +
        "Accept: application/json\r\n" +
        "Content-Type: application/json\r\n" +
        "Content-Length: " + std::to_string(json_body.length()) + "\r\n" +
        "\r\n" +
        json_body;

    ssize_t bytes_sent = send(sock, request.c_str(), request.length(), 0);
    if (bytes_sent < 0) {
        std::cerr << "[-] Failed to send payload to " << endpoint << "\n";
        close(sock);
        return false;
    }

    // Read response back from Firecracker
    char buffer[2048];
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);

    if (bytes_read > 0) {
        std::string response(buffer);
        if (is_http_success(response)) {
            std::cout << "[+] Configured " << endpoint << " successfully.\n";
            return true;
        } else {
            std::cerr << "[-] Firecracker rejected " << endpoint << ":\n" << response << "\n";
            return false;
        }
    }
    
    return false;
}

int main() {
    // --- ADJUST THESE PATHS TO YOUR LOCAL SETUP ---
    const std::string KERNEL_PATH = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/vmlinux/ubuntu-vmlinux.bin";
    const std::string ROOTFS_PATH = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/rootfs/ubuntu-rootfs.ext4";
    const std::string SOCKET_PATH = "/tmp/fc_test.socket";
    // ----------------------------------------------

    // Remove any stale socket file from previous runs
    unlink(SOCKET_PATH.c_str());

    std::cout << "[*] Spawning Firecracker process...\n";
    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "[-] Fork failed.\n";
        return 1;
    }

    if (pid == 0) {
        // Child Process: Launch Firecracker binary
        execlp("firecracker", "firecracker", "--api-sock", SOCKET_PATH.c_str(), (char*)NULL); // child process is converted to firecracker process
        std::cerr << "[-] Failed to exec firecracker. Is it in your PATH?\n";
        _exit(1);
    }

    // Parent Process: Wait for Firecracker's Unix socket to appear
    std::cout << "[*] Waiting for API socket: " << SOCKET_PATH << "...\n";
    bool socket_ready = false;
    for (int i = 0; i < 20; ++i) {
        if (access(SOCKET_PATH.c_str(), F_OK) == 0) {
            socket_ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } // wait for total 20x50 milliseconds = 1000 milliseconds

    if (!socket_ready) {
        std::cerr << "[-] Socket timeout. Firecracker failed to start.\n";
        kill(pid, SIGTERM);
        return 1;
    }

    std::cout << "[+] Firecracker running with PID " << pid << "\n";

    // The following HTTP/1.1 requests are sent to the parallelly running microvm
    // 1. Set Boot Source (Kernel + Kernel Args)
    std::string boot_source_json = 
        "{\"kernel_image_path\": \"" + KERNEL_PATH + "\", "
        "\"boot_args\": \"console=ttyS0 reboot=k panic=1 pci=off\"}";
    if (!send_firecracker_put(SOCKET_PATH, "/boot-source", boot_source_json)) return 1;

    // 2. Set Root Drive
    std::string rootfs_json = 
        "{\"drive_id\": \"rootfs\", "
        "\"path_on_host\": \"" + ROOTFS_PATH + "\", "
        "\"is_root_device\": true, "
        "\"is_read_only\": false}";
    if (!send_firecracker_put(SOCKET_PATH, "/drives/rootfs", rootfs_json)) return 1;

    // 3. Set Machine Configuration (1 vCPU, 512 MB RAM)
    std::string machine_config_json = 
        "{\"vcpu_count\": 1, \"mem_size_mib\": 512}";
    if (!send_firecracker_put(SOCKET_PATH, "/machine-config", machine_config_json)) return 1;

    // 4. Start the Instance
    std::cout << "[*] Sending InstanceStart action...\n";
    std::string start_json = "{\"action_type\": \"InstanceStart\"}";
    if (!send_firecracker_put(SOCKET_PATH, "/actions", start_json)) return 1;

    std::cout << "[+] MicroVM is booting! Waiting 5 seconds before cleanup...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Cleanup for test: Kill the process and remove socket
    std::cout << "[*] Tearing down test VM...\n";
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    unlink(SOCKET_PATH.c_str());
    std::cout << "[+] Done.\n";

    return 0;
}