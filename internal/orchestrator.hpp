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
#include "database.hpp" // Include the new database wrapper

namespace fs = std::filesystem;

struct MicroVM {
    int id;
    pid_t pid;
    std::string ip;
    std::string host_ip; // Store host IP for the /30 subnet
    std::string mac;
    std::string tap_name;
    std::string socket_path;
    std::string status;
    int vcpus;
    int mem_mib;
    bool ssh_exposed = false;
    int host_port = 0;
};

class VMManager {
private:
    std::unordered_map<int, MicroVM> vms;
    std::unordered_map<pid_t, int> pid_to_slot; // Tracks children spawned this session
    std::mutex mtx;
    Database db;

    std::string generate_mac(int slot) {
        std::stringstream ss;
        ss << "AA:FC:AC:10:" << std::setfill('0') << std::setw(2) << std::hex << (slot / 256) 
           << ":" << std::setfill('0') << std::setw(2) << std::hex << (slot % 256);
        return ss.str();
    }

    void cleanup_vm_resources(MicroVM& vm) {
        std::cout << "[*] Cleaning up resources for VM " << vm.id << "...\n";
        teardown_cryo_network(vm.tap_name);
        if (vm.ssh_exposed) {
            unexpose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        }
        unlink(vm.socket_path.c_str());
        db.remove_vm(vm.id);
    }

    void reconcile_state() {
        std::cout << "[*] Reconciling state from database...\n";
        auto records = db.get_all_vms();
        
        for (const auto& r : records) {
            // Check if the process is actually alive
            if (kill(r.pid, 0) == 0) {
                std::cout << "[+] Adopted running VM " << r.slot << " (PID " << r.pid << ")\n";
                MicroVM vm;
                vm.id = r.slot;
                vm.pid = r.pid;
                vm.status = r.status;
                vm.tap_name = r.tap_name;
                vm.ssh_exposed = r.ssh_exposed;
                vm.host_port = r.host_port;
                vm.ip = r.guest_ip;
                vm.socket_path = r.socket_path;
                
                vms[r.slot] = vm; 
                // Note: We DO NOT add to pid_to_slot because waitpid cannot track adopted orphans.
            } else {
                std::cout << "[-] Cleaning up stale DB entry for VM " << r.slot << "\n";
                MicroVM stale_vm;
                stale_vm.id = r.slot;
                stale_vm.tap_name = r.tap_name;
                stale_vm.ssh_exposed = r.ssh_exposed;
                stale_vm.host_port = r.host_port;
                stale_vm.ip = r.guest_ip;
                stale_vm.socket_path = r.socket_path;
                cleanup_vm_resources(stale_vm);
            }
        }
    }

    void start_reaper() {
        std::thread([this]() {
            while (true) {
                // 1. Reap processes spawned in this session
                int status;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                
                if (pid > 0) {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (pid_to_slot.find(pid) != pid_to_slot.end()) {
                        int slot = pid_to_slot[pid];
                        cleanup_vm_resources(vms[slot]);
                        vms.erase(slot);
                        pid_to_slot.erase(pid);
                    }
                }

                // 2. Poll processes adopted from a previous session
                std::vector<int> dead_adopted_slots;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& pair : vms) {
                        if (pid_to_slot.find(pair.second.pid) == pid_to_slot.end()) {
                            // It is an adopted orphan, verify via kill
                            if (kill(pair.second.pid, 0) != 0) {
                                dead_adopted_slots.push_back(pair.first);
                            }
                        }
                    }
                }

                for (int slot : dead_adopted_slots) {
                    std::lock_guard<std::mutex> lock(mtx);
                    cleanup_vm_resources(vms[slot]);
                    vms.erase(slot);
                }

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }).detach();
    }

public:
    std::string active_iface;

    VMManager() {
        active_iface = get_default_interface();
        if (active_iface.empty()) {
            std::cerr << "[-] Warning: Failed to detect active internet interface.\n";
        }
        reconcile_state(); // Sync with DB before starting
        start_reaper();
    }

    MicroVM create_vm(int vcpus, int mem_mib, bool expose_ssh) {
        std::lock_guard<std::mutex> lock(mtx);

        int slot = db.get_next_free_slot();

        // Zero-Waste /30 IP Math Calculation
        int base = slot * 4;
        int octet3 = base / 256;
        int octet4 = base % 256;
        
        MicroVM vm;
        vm.id = slot;
        vm.host_ip = "172.16." + std::to_string(octet3) + "." + std::to_string(octet4 + 1);
        vm.ip = "172.16." + std::to_string(octet3) + "." + std::to_string(octet4 + 2);
        
        vm.mac = generate_mac(slot);
        vm.tap_name = "cryo" + std::to_string(slot);
        vm.socket_path = "/tmp/cryo_" + std::to_string(slot) + ".socket";
        vm.vcpus = vcpus;
        vm.mem_mib = mem_mib;
        vm.status = "booting";
        vm.ssh_exposed = expose_ssh;
        if (expose_ssh) vm.host_port = 2200 + slot;

        setup_cryo_network(vm.tap_name, vm.host_ip, active_iface);
        if (expose_ssh) expose_vm_port(active_iface, vm.host_port, vm.ip, 22);

        unlink(vm.socket_path.c_str());
        pid_t pid = fork();

        if (pid == 0) {
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
        pid_to_slot[pid] = vm.id;
        
        // Persist to database
        db.insert_vm(slot, pid, "booting", vcpus, mem_mib, vm.ip, vm.host_ip, vm.tap_name, expose_ssh, vm.host_port);

        return vm;
    }

    bool configure_and_start(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            vm = vms[id];
        }

        // 1. Wait for Unix Domain Socket creation
        bool ready = false;
        for (int i = 0; i < 30; ++i) {
            if (access(vm.socket_path.c_str(), F_OK) == 0) { 
                ready = true; 
                break; 
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!ready) {
            std::cerr << "[-] Socket creation timed out for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // 2. Set Boot Source
        std::string boot_args = "console=ttyS0 reboot=k panic=1 pci=off ip=" + 
                                vm.ip + "::" + vm.host_ip + ":255.255.255.252::eth0:off";
        
        std::string boot_payload = "{\"kernel_image_path\": \"" + kernel + "\", \"boot_args\": \"" + boot_args + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/boot-source", boot_payload)) {
            std::cerr << "[-] Failed to set boot source for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // 3. Attach Root Filesystem Drive
        std::string drive_payload = "{\"drive_id\": \"rootfs\", \"path_on_host\": \"" + rootfs + "\", \"is_root_device\": true, \"is_read_only\": false}";
        if (!send_firecracker_put(vm.socket_path, "/drives/rootfs", drive_payload)) {
            std::cerr << "[-] Failed to attach rootfs for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // 4. Configure Virtual Network Interface (TAP)
        std::string net_payload = "{\"iface_id\": \"eth0\", \"guest_mac\": \"" + vm.mac + "\", \"host_dev_name\": \"" + vm.tap_name + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/network-interfaces/eth0", net_payload)) {
            std::cerr << "[-] Failed to configure network interface for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // 5. Configure vCPU and Memory
        std::string machine_payload = "{\"vcpu_count\": " + std::to_string(vm.vcpus) + ", \"mem_size_mib\": " + std::to_string(vm.mem_mib) + "}";
        if (!send_firecracker_put(vm.socket_path, "/machine-config", machine_payload)) {
            std::cerr << "[-] Failed to set machine config for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // 6. Issue InstanceStart Command
        if (!send_firecracker_put(vm.socket_path, "/actions", "{\"action_type\": \"InstanceStart\"}")) {
            std::cerr << "[-] Failed to issue InstanceStart for VM " << id << "\n";
            terminate_vm(id);
            return false;
        }

        // Mark as running in memory and DB
        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].status = "running";
            db.update_status(id, "running");
        }
        
        std::cout << "[+] VM " << id << " successfully booted and running!\n";
        return true;
    }

    void terminate_vm(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) == vms.end()) return;
        
        MicroVM& vm = vms[id];
        std::cout << "[*] Terminating VM " << id << "...\n";
        
        kill(vm.pid, SIGTERM);
        // We no longer need waitpid here, the Reaper thread handles it automatically!
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
