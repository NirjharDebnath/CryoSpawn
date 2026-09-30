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
#include "database.hpp" 

namespace fs = std::filesystem;

struct MicroVM {
    int id = 0;
    pid_t pid = 0;
    std::string ip = ""; 
    std::string host_ip = ""; 
    std::string mac = "";
    std::string tap_name = ""; 
    std::string socket_path = ""; 
    std::string status = "stopped";
    int vcpus = 1; 
    int mem_mib = 512; 
    bool ssh_exposed = false; 
    bool http_exposed = false;
    bool https_exposed = false;
    int host_port = 0;
    std::string project_name = "";
};

class VMManager {
private:
    std::unordered_map<int, MicroVM> vms;
    std::unordered_map<pid_t, int> pid_to_slot;
    std::mutex mtx;
    Database db;

    std::string generate_mac(int slot) {
        std::stringstream ss;
        ss << "AA:FC:AC:10:" << std::setfill('0') << std::setw(2) << std::hex << (slot / 256) << ":" << std::setfill('0') << std::setw(2) << std::hex << (slot % 256);
        return ss.str();
    }

    void cleanup_vm_resources(MicroVM& vm) {
        std::cout << "[*] Cleaning up resources for VM " << vm.id << "...\n";
        auto links = db.get_all_links();
        for (const auto& link : links) {
            if (link.vm1_id == vm.id) unlink_taps(vm.tap_name, "cryo" + std::to_string(link.vm2_id));
            if (link.vm2_id == vm.id) unlink_taps(vm.tap_name, "cryo" + std::to_string(link.vm1_id));
        }
        teardown_cryo_network(vm.tap_name);
        if (vm.ssh_exposed) unexpose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        unlink(vm.socket_path.c_str());

        // Delete the unique rootfs disk for this VM to free SSD space
        std::string base_dir = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances";
        std::string vm_rootfs = base_dir + "/vm_" + std::to_string(vm.id) + "_rootfs.ext4";
        std::string vm_snap = base_dir + "/vm_" + std::to_string(vm.id) + "_state.snap";
        std::string vm_mem = base_dir + "/vm_" + std::to_string(vm.id) + "_mem.ram";

        if (std::filesystem::exists(vm_rootfs)) {
            std::filesystem::remove(vm_rootfs);
            std::cout << "[+] Deleted VM disk to free SSD space: " << vm_rootfs << "\n";
        }
        if (std::filesystem::exists(vm_snap)) {
            std::filesystem::remove(vm_snap);
            std::cout << "[+] Deleted VM state snapshot: " << vm_snap << "\n";
        }
        if (std::filesystem::exists(vm_mem)) {
            std::filesystem::remove(vm_mem);
            std::cout << "[+] Deleted VM RAM snapshot: " << vm_mem << "\n";
        }

        // Clean up Unix Domain Sockets for Ghost Proxy
        unlink(("/tmp/cryo_vm_" + std::to_string(vm.id) + "_22.sock").c_str());
        unlink(("/tmp/cryo_vm_" + std::to_string(vm.id) + "_80.sock").c_str());
        unlink(("/tmp/cryo_vm_" + std::to_string(vm.id) + "_443.sock").c_str());

        db.remove_vm(vm.id);
    }

    void reconcile_state() {
        std::cout << "[*] Reconciling state from database...\n";
        auto records = db.get_all_vms();
        for (const auto& r : records) {
            if (kill(r.pid, 0) == 0) {
                std::cout << "[+] Adopted running VM " << r.slot << " (PID " << r.pid << ")\n";
                MicroVM vm;
                vm.id = r.slot; 
                vm.pid = r.pid; 
                vm.status = r.status;
                vm.vcpus = r.vcpus; 
                vm.mem_mib = r.mem_mib; 
                vm.ip = r.guest_ip; 
                vm.host_ip = r.host_ip;
                vm.tap_name = r.tap_name; 
                vm.ssh_exposed = r.ssh_exposed;
                vm.http_exposed = r.http_exposed;
                vm.https_exposed = r.https_exposed;
                vm.host_port = r.host_port;
                vm.project_name = r.project_name; 
                vm.socket_path = r.socket_path;
                vms[r.slot] = vm; 
                pid_to_slot[r.pid] = r.slot;
            } else {
                std::cout << "[-] Cleaning up stale DB entry for VM " << r.slot << "\n";
                MicroVM stale_vm; 
                stale_vm.id = r.slot; 
                stale_vm.tap_name = r.tap_name;
                stale_vm.ssh_exposed = r.ssh_exposed; 
                stale_vm.http_exposed = r.http_exposed;
                stale_vm.https_exposed = r.https_exposed;
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
                int status; pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid > 0) {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (pid_to_slot.find(pid) != pid_to_slot.end()) {
                        int slot = pid_to_slot[pid];
                        if (vms[slot].status == "hibernated") {
                            // Leave resources intact, just clear PID
                            vms[slot].pid = 0;
                            pid_to_slot.erase(pid);
                        } else {
                            cleanup_vm_resources(vms[slot]);
                            vms.erase(slot); pid_to_slot.erase(pid);
                        }
                    }
                }
                std::vector<int> dead_adopted;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& pair : vms) {
                        if (pair.second.status == "hibernated") continue; // Skip hibernated VMs!
                        if (pid_to_slot.find(pair.second.pid) == pid_to_slot.end()) {
                            if (pair.second.pid > 0 && kill(pair.second.pid, 0) != 0) dead_adopted.push_back(pair.first);
                        }
                    }
                }
                for (int slot : dead_adopted) {
                    std::lock_guard<std::mutex> lock(mtx);
                    cleanup_vm_resources(vms[slot]); vms.erase(slot);
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }).detach();
    }

public:
    std::string active_iface;

    VMManager() {
        active_iface = get_default_interface();
        init_cryo_firewall_baseline(active_iface);
        std::filesystem::create_directories("/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances");
        reconcile_state(); 
        start_reaper();
    }

    MicroVM create_vm(int vcpus, int mem_mib, bool expose_ssh, bool expose_http, bool expose_https, const std::string& project) {
        std::lock_guard<std::mutex> lock(mtx);
        int slot = db.get_next_free_slot(); int base = slot * 4;
        
        MicroVM vm; vm.id = slot;
        vm.host_ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 1);
        vm.ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 2);
        vm.mac = generate_mac(slot); vm.tap_name = "cryo" + std::to_string(slot);
        vm.socket_path = "/tmp/cryo_" + std::to_string(slot) + ".socket";
        vm.vcpus = vcpus; vm.mem_mib = mem_mib; vm.status = "booting";
        vm.ssh_exposed = expose_ssh; 
        vm.http_exposed = expose_http;
        vm.https_exposed = expose_https;
        vm.project_name = project;
        if (expose_ssh) vm.host_port = 2200 + slot;

        setup_cryo_network(vm.tap_name, vm.host_ip, active_iface);
        // Bypassing iptables to use Ghost Proxy!

        unlink(vm.socket_path.c_str());
        pid_t pid = fork();
        if (pid == 0) {
            setsid(); // PREVENTS CTRL+C ON DAEMON FROM KILLING MICROVM
            int dev_null = open("/dev/null", O_RDWR);
            dup2(dev_null, STDOUT_FILENO); dup2(dev_null, STDERR_FILENO); dup2(dev_null, STDIN_FILENO); close(dev_null);
            execlp("firecracker", "firecracker", "--api-sock", vm.socket_path.c_str(), (char*)NULL);
            _exit(1);
        }

        vm.pid = pid; vms[vm.id] = vm; pid_to_slot[pid] = vm.id;
        db.insert_vm(slot, pid, "booting", vcpus, mem_mib, vm.ip, vm.host_ip, vm.tap_name, expose_ssh, vm.host_port, project, expose_http, expose_https);
        return vm;
    }

    bool configure_and_start(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            vm = vms[id];
        }

        bool ready = false;
        for (int i = 0; i < 30; ++i) {
            if (access(vm.socket_path.c_str(), F_OK) == 0) { ready = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) { terminate_vm(id); return false; }

        std::string boot_args = "console=ttyS0 reboot=k panic=1 pci=off root=/dev/vda rw ip=" + vm.ip + "::" + vm.host_ip + ":255.255.255.252::eth0:off";
        std::string boot_payload = "{\"kernel_image_path\": \"" + kernel + "\", \"boot_args\": \"" + boot_args + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/boot-source", boot_payload)) { terminate_vm(id); return false; }

        // 1. Create a unique rootfs copy for this VM
        std::filesystem::path base_path(rootfs);
        std::filesystem::path vm_rootfs = std::filesystem::path("/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances") / ("vm_" + std::to_string(id) + "_rootfs.ext4");
        
        try {
            std::cout << "[*] Cloning disk image for VM " << id << "...\n";
            std::filesystem::copy(base_path, vm_rootfs, std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            std::cerr << "[-] Failed to clone rootfs: " << e.what() << "\n";
            terminate_vm(id);
            return false;
        }

        // 2. Tell Firecracker to use the NEW instance disk, not the base template
        std::string drive_payload = "{\"drive_id\": \"rootfs\", \"path_on_host\": \"" + vm_rootfs.string() + "\", \"is_root_device\": true, \"is_read_only\": false}";
        if (!send_firecracker_put(vm.socket_path, "/drives/rootfs", drive_payload)) { terminate_vm(id); return false; }

        std::string net_payload = "{\"iface_id\": \"eth0\", \"guest_mac\": \"" + vm.mac + "\", \"host_dev_name\": \"" + vm.tap_name + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/network-interfaces/eth0", net_payload)) { terminate_vm(id); return false; }

        std::string machine_payload = "{\"vcpu_count\": " + std::to_string(vm.vcpus) + ", \"mem_size_mib\": " + std::to_string(vm.mem_mib) + "}";
        if (!send_firecracker_put(vm.socket_path, "/machine-config", machine_payload)) { terminate_vm(id); return false; }

        if (!send_firecracker_put(vm.socket_path, "/actions", "{\"action_type\": \"InstanceStart\"}")) { terminate_vm(id); return false; }

        { std::lock_guard<std::mutex> lock(mtx); vms[id].status = "running"; db.update_status(id, "running"); }
        return true;
    }

    void terminate_vm(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) == vms.end()) return;
        
        if (vms[id].pid > 0) {
            kill(vms[id].pid, SIGTERM);
        } else if (vms[id].status == "hibernated") {
            cleanup_vm_resources(vms[id]);
            vms.erase(id);
        }
    }

    bool hibernate_vm(int id) {
        std::string snap_path = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances/vm_" + std::to_string(id) + "_state.snap";
        std::string mem_path = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances/vm_" + std::to_string(id) + "_mem.ram";
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status != "running") return false;
            
            // 1. Pause VM execution
            if (!send_firecracker_patch(vms[id].socket_path, "/vm", "{\"state\": \"Paused\"}")) return false;
            
            // 2. Dump RAM and CPU state to disk
            std::string snap_payload = "{\"snapshot_type\": \"Full\", \"snapshot_path\": \"" + snap_path + "\", \"mem_file_path\": \"" + mem_path + "\"}";
            if (!send_firecracker_put(vms[id].socket_path, "/snapshot/create", snap_payload)) return false;
            
            // 3. Mark as hibernated BEFORE killing, so Reaper doesn't delete the network/disk!
            vms[id].status = "hibernated"; 
            db.update_status(id, "hibernated");
            
            // 4. Kill the Firecracker process to free host RAM
            kill(vms[id].pid, SIGTERM);
        }
        return true;
    }

    bool wake_vm(int id) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status != "hibernated") return false;
            vms[id].status = "waking"; // Prevent race condition from concurrent Ghost Proxy threads!
            db.update_status(id, "waking");
            vm = vms[id];
        }

        std::string snap_path = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances/vm_" + std::to_string(id) + "_state.snap";
        std::string mem_path = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/instances/vm_" + std::to_string(id) + "_mem.ram";

        // 1. Start a fresh Firecracker process
        unlink(vm.socket_path.c_str());
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            int dev_null = open("/dev/null", O_RDWR);
            dup2(dev_null, STDOUT_FILENO); dup2(dev_null, STDERR_FILENO); dup2(dev_null, STDIN_FILENO); close(dev_null);
            execlp("firecracker", "firecracker", "--api-sock", vm.socket_path.c_str(), (char*)NULL);
            _exit(1);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].pid = pid;
            pid_to_slot[pid] = id;
        }

        // Wait for socket
        bool ready = false;
        for (int i = 0; i < 30; ++i) {
            if (access(vm.socket_path.c_str(), F_OK) == 0) { ready = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) return false;

        // 2. Load the Snapshot back into memory
        std::string load_payload = "{\"snapshot_path\": \"" + snap_path + "\", \"mem_file_path\": \"" + mem_path + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/snapshot/load", load_payload)) { terminate_vm(id); return false; }

        // 3. Un-pause the VM
        if (!send_firecracker_patch(vm.socket_path, "/vm", "{\"state\": \"Resumed\"}")) { terminate_vm(id); return false; }

        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].status = "running";
            db.update_status(id, "running");
        }
        return true;
    }

    std::vector<MicroVM> list_vms() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<MicroVM> list;
        for (const auto& pair : vms) list.push_back(pair.second);
        return list;
    }

    bool link_microvms(int id1, int id2) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        if (vms[id1].project_name != vms[id2].project_name) return false; 
        db.add_link(vms[id1].project_name, id1, id2);
        link_taps(vms[id1].tap_name, vms[id2].tap_name);
        return true;
    }

    bool unlink_microvms(int id1, int id2) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        db.remove_link(id1, id2);
        unlink_taps(vms[id1].tap_name, vms[id2].tap_name);
        return true;
    }

    std::vector<Database::LinkRow> get_all_links() { return db.get_all_links(); }
};

