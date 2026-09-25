#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iomanip>
#include <sstream>

#include "api.hpp"
#include "network.hpp"

namespace fs = std::filesystem;

struct MicroVM {
    int id;
    pid_t pid;
    std::string ip;
    std::string mac;
    std::string tap_name;
    std::string socket_path;
    std::string status; // "running", "stopped", "crashed"
    int vcpus;
    int mem_mib;
    bool ssh_exposed = false;
    int host_port = 0;
};

class VMManager {
private:
    std::unordered_map<int, MicroVM> vms;
    std::vector<bool> ip_slots; // Tracks available IPs (2 to 254)
    std::mutex mtx;

    int allocate_slot(int requested_slot = -1) {
    if (requested_slot >= 2 && requested_slot <= 254) {
        if (!ip_slots[requested_slot]) {
            ip_slots[requested_slot] = true;
            return requested_slot;
        }
        throw std::runtime_error("Requested IP slot is already in use");
    }
    // Auto-assign the first free slot
    for (int i = 2; i <= 254; ++i) {
            if (!ip_slots[i]) {
                ip_slots[i] = true;
                return i;
            }
        }
        return -1; // Pool exhausted
    }

    void free_slot(int slot) {
        if (slot >= 2 && slot <= 254) {
            ip_slots[slot] = false;
        }
    }

    std::string generate_mac(int slot) {
        // AA:FC:AC:10:01:XX (where XX is the hex of the slot)
        std::stringstream ss;
        ss << "AA:FC:AC:10:01:" << std::setfill('0') << std::setw(2) << std::hex << slot;
        return ss.str();
    }

public:
    std::string active_iface;

    VMManager() : ip_slots(255, false) {
        active_iface = get_default_interface();
        if (active_iface.empty()) {
            std::cerr << "[-] Warning: Failed to detect active internet interface.\n";
        }
    }

    MicroVM create_vm(int vcpus, int mem_mib, bool expose_ssh, int manual_slot) {
        std::lock_guard<std::mutex> lock(mtx);

        int slot = allocate_slot();
        if (slot == -1) throw std::runtime_error("No available IP slots");

        MicroVM vm;
        vm.id = slot;
        // Use the slot ID for the subnet (e.g., 172.16.2.2)
        vm.ip = "172.16." + std::to_string(slot) + ".2";
        std::string host_ip = "172.16." + std::to_string(slot) + ".1";
        
        vm.mac = generate_mac(slot);
        vm.tap_name = "cryo" + std::to_string(slot);
        vm.socket_path = "/tmp/cryo_" + std::to_string(slot) + ".socket";
        vm.vcpus = vcpus;
        vm.mem_mib = mem_mib;
        vm.status = "booting";

        vm.ssh_exposed = expose_ssh;
        if (expose_ssh) {
            vm.host_port = 2200 + slot; // e.g., slot 2 -> port 2202
            expose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        }

        // Pass the dynamic host_ip instead of the hardcoded "172.16.1.1"
        setup_cryo_network(vm.tap_name, host_ip, active_iface);

        // Setup Networking for this specific VM
        setup_cryo_network(vm.tap_name, "172.16.1.1", active_iface);

        unlink(vm.socket_path.c_str());
        pid_t pid = fork();

        if (pid == 0) {
            // Child
            std::string log_path = "/tmp/cryo_" + std::to_string(slot) + ".log";
            int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
            
            int dev_null = open("/dev/null", O_RDONLY);
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);

            execlp("firecracker", "firecracker", "--api-sock", vm.socket_path.c_str(), (char*)NULL);
            _exit(1);
        }

        vm.pid = pid;
        vms[vm.id] = vm;

        return vm; // Return preliminary info so HTTP request doesn't hang forever
    }

    bool configure_and_start(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            vm = vms[id];
        }

        // Wait for socket
        bool ready = false;
        for (int i = 0; i < 30; ++i) {
            if (access(vm.socket_path.c_str(), F_OK) == 0) { ready = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) return false;

        // Reconstruct the unique host gateway IP for this specific slot
        std::string host_ip = "172.16." + std::to_string(id) + ".1";
        
        std::string boot_args = "console=ttyS0 reboot=k panic=1 pci=off ip=" + 
                                vm.ip + "::" + host_ip + ":255.255.255.0::eth0:off";
        
        send_firecracker_put(vm.socket_path, "/boot-source", 
            "{\"kernel_image_path\": \"" + kernel + "\", \"boot_args\": \"" + boot_args + "\"}");
            
        send_firecracker_put(vm.socket_path, "/drives/rootfs", 
            "{\"drive_id\": \"rootfs\", \"path_on_host\": \"" + rootfs + "\", \"is_root_device\": true, \"is_read_only\": false}");
            
        send_firecracker_put(vm.socket_path, "/network-interfaces/eth0", 
            "{\"iface_id\": \"eth0\", \"guest_mac\": \"" + vm.mac + "\", \"host_dev_name\": \"" + vm.tap_name + "\"}");
            
        send_firecracker_put(vm.socket_path, "/machine-config", 
            "{\"vcpu_count\": " + std::to_string(vm.vcpus) + ", \"mem_size_mib\": " + std::to_string(vm.mem_mib) + "}");
            
        send_firecracker_put(vm.socket_path, "/actions", "{\"action_type\": \"InstanceStart\"}");

        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].status = "running";
        }
        return true;
    }

    void terminate_vm(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) == vms.end()) return;
        
        MicroVM& vm = vms[id];
        std::cout << "[*] Terminating VM " << id << "...\n";
        
        kill(vm.pid, SIGTERM);
        waitpid(vm.pid, NULL, WNOHANG); // Non-blocking wait to clean up zombie
        
        unlink(vm.socket_path.c_str());
        teardown_cryo_network(vm.tap_name);

        if (vm.ssh_exposed) {
            unexpose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        }
        
        free_slot(id);
        vms.erase(id);
    }

    std::vector<MicroVM> list_vms() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<MicroVM> list;
        for (const auto& pair : vms) {
            list.push_back(pair.second);
        }
        return list;
    }
};