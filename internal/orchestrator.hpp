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
#include <random>
#include <unordered_set>

#include "api.hpp"
#include "network.hpp"
#include "config.hpp"
#include "database.hpp" 

namespace fs = std::filesystem;

struct MicroVM {
    int owner_id;
    std::string uuid;
    std::string name;
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
    int rootfs_gb = 1;
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

    std::string generate_vol_id() {
        std::random_device rd;
        std::mt19937 gen(rd()); 
        std::uniform_int_distribution<> dis(0, 15);
        const char* hex_chars = "0123456789abcdef";
        std::string id = "vol-";
        for (int i = 0; i < 8; ++i) id += hex_chars[dis(gen)];
        return id;
    }

    std::string generate_mac(int slot) {
        std::stringstream ss;
        ss << "AA:FC:AC:10:" << std::setfill('0') << std::setw(2) << std::hex << (slot / 256) << ":" << std::setfill('0') << std::setw(2) << std::hex << (slot % 256);
        return ss.str();
    }

public:
    Database db;
    std::unordered_set<std::string> active_uuids;

    std::string get_hex(const std::string& id) {
        if (id.length() > 3 && id.substr(0,3) == "vm-") return id.substr(3);
        if (id.length() > 4 && id.substr(0,4) == "vol-") return id.substr(4);
        return id;
    }

    std::string generate_vm_id() {
        std::random_device rd;
        std::mt19937 gen(rd()); 
        std::uniform_int_distribution<> dis(0, 15);
        const char* hex_chars = "0123456789abcdef";
        std::string id = "vm-";
        for (int i = 0; i < 8; ++i) id += hex_chars[dis(gen)];
        return id;
    }

    int get_slot_by_uuid(const std::string& uuid) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& [slot, vm] : vms) {
            if (vm.uuid == uuid) return slot;
        }
        return -1;
    }


    // The janitor. Sweeps up all the network taps, sockets, and deletes the ext4 disk.
void cleanup_vm_resources(MicroVM& vm) {
        std::cout << "[*] Cleaning up resources for VM " << vm.id << "...\n";
        auto links = db.get_all_links();
        for (const auto& link : links) {
            if (link.vm1_id == vm.id && vms.count(link.vm2_id)) unlink_taps(vm.tap_name, vms[link.vm2_id].tap_name);
            if (link.vm2_id == vm.id && vms.count(link.vm1_id)) unlink_taps(vm.tap_name, vms[link.vm1_id].tap_name);
        }
        teardown_cryo_network(vm.tap_name);
        if (vm.ssh_exposed) unexpose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        unlink(vm.socket_path.c_str());

        // Delete the unique rootfs disk for this VM to free SSD space
        std::string base_dir = config::BASE_PATH + "/instances";
        std::string vm_rootfs = base_dir + "/cryo-" + get_hex(vm.uuid) + "_rootfs.ext4";
        std::string vm_snap = base_dir + "/cryo-" + get_hex(vm.uuid) + "_state.snap";
        std::string vm_mem = base_dir + "/cryo-" + get_hex(vm.uuid) + "_mem.ram";

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
        unlink(("/tmp/cryo-" + get_hex(vm.uuid) + "_22.sock").c_str());
        unlink(("/tmp/cryo-" + get_hex(vm.uuid) + "_80.sock").c_str());
        unlink(("/tmp/cryo-" + get_hex(vm.uuid) + "_443.sock").c_str());

        db.remove_vm(vm.id);
    }

    void reconcile_state() {
        std::cout << "[*] Reconciling state from database...\n";
        auto records = db.get_all_vms();
        for (const auto& r : records) {
            if (kill(r.pid, 0) == 0 || r.status == "hibernated") {
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
                vm.rootfs_gb = r.rootfs_gb;
                vm.host_port = r.host_port;
                vm.project_name = r.project_name; 
                vm.socket_path = r.socket_path;
                vm.uuid = r.uuid;
                vm.name = r.name;
                vm.owner_id = r.owner_id;
                if (vm.uuid.empty()) {
                    vm.uuid = generate_vm_id();
                    if (vm.name.empty()) vm.name = "legacy-vm-" + std::to_string(r.slot);
                    db.update_uuid_and_name(vm.id, vm.uuid, vm.name);
                }
                active_uuids.insert(vm.uuid);
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
                        } else if (vms[slot].status == "restarting") {
                            // Leave resources intact for reboot!
                            vms[slot].pid = 0;
                            pid_to_slot.erase(pid);
                        } else {
                            cleanup_vm_resources(vms[slot]);
                            active_uuids.erase(vms[slot].uuid); vms.erase(slot); pid_to_slot.erase(pid);
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
        std::filesystem::create_directories(config::BASE_PATH + "/instances");
        std::filesystem::create_directories(config::BASE_PATH + "/volumes");
        reconcile_state(); 
        start_reaper();
    }

    // Provisions the basic info for a new VM (IP, UUID, etc) and saves it to SQLite.
MicroVM create_vm(int vcpus, int mem_mib, bool expose_ssh, bool expose_http, bool expose_https, const std::string& project, int rootfs_gb, const std::string& name) {
        std::lock_guard<std::mutex> lock(mtx);
        int slot = db.get_next_free_slot(); int base = slot * 4;
        
        MicroVM vm; vm.id = slot;
        vm.name = name;
        do {
            vm.uuid = generate_vm_id();
        } while (active_uuids.find(vm.uuid) != active_uuids.end());
        active_uuids.insert(vm.uuid);
        vm.host_ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 1);
        vm.ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 2);
        vm.mac = generate_mac(slot); vm.tap_name = "cryo-" + get_hex(vm.uuid);
        vm.socket_path = "/tmp/cryo-" + get_hex(vm.uuid) + ".socket";
        vm.vcpus = vcpus; vm.mem_mib = mem_mib; vm.status = "booting";
        vm.ssh_exposed = expose_ssh; 
        vm.http_exposed = expose_http;
        vm.https_exposed = expose_https;
        vm.project_name = project; vm.rootfs_gb = rootfs_gb;
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
        db.insert_vm(slot, pid, "booting", vcpus, mem_mib, vm.ip, vm.host_ip, vm.tap_name, expose_ssh, vm.host_port, project, expose_http, expose_https, rootfs_gb, vm.uuid, vm.name);
        return vm;
    }

    // The heavy lifter. Clones the rootfs, fires up Firecracker, and boots the Linux kernel.
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
        std::string vm_uuid = vms[id].uuid;
        std::filesystem::path vm_rootfs = std::filesystem::path(config::BASE_PATH + "/instances") / ("cryo-" + get_hex(vm_uuid) + "_rootfs.ext4");
        
        try {
            if (!std::filesystem::exists(vm_rootfs)) {
                std::cout << "[*] Cloning disk image for VM " << id << "...\n";
                std::filesystem::copy(base_path, vm_rootfs);
                if (vm.rootfs_gb >= 3) {
                    std::cout << "[*] Expanding rootfs to " << vm.rootfs_gb << "GB...\n";
                    system(("truncate -s " + std::to_string(vm.rootfs_gb) + "G \"" + vm_rootfs.string() + "\"").c_str());
                    system(("e2fsck -fy \"" + vm_rootfs.string() + "\" > /dev/null 2>&1").c_str());
                    system(("resize2fs \"" + vm_rootfs.string() + "\" > /dev/null 2>&1").c_str());
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[-] Failed to clone rootfs: " << e.what() << "\n";
            terminate_vm(id);
            return false;
        }

        // 2. Tell Firecracker to use the NEW instance disk, not the base template
        std::string drive_payload = "{\"drive_id\": \"rootfs\", \"path_on_host\": \"" + vm_rootfs.string() + "\", \"is_root_device\": true, \"is_read_only\": false}";
        if (!send_firecracker_put(vm.socket_path, "/drives/rootfs", drive_payload)) { terminate_vm(id); return false; }

        // 3. Attach any assigned Elastic Volumes
        auto all_vols = db.get_volumes();
        for (const auto& vol : all_vols) {
            if (vol.attached_vm_id == id) {
                std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol.id) + ".ext4";
                std::string vol_payload = "{\"drive_id\": \"vol_" + std::to_string(vol.id) + "\", \"path_on_host\": \"" + vol_path + "\", \"is_root_device\": false, \"is_read_only\": false}";
                if (!send_firecracker_put(vm.socket_path, "/drives/vol_" + std::to_string(vol.id), vol_payload)) {
                    std::cerr << "[-] Failed to attach volume " << vol.id << " to VM " << id << "\n";
                } else {
                    std::cout << "[+] Attached Volume " << vol.id << " (" << vol.name << ") to VM " << id << "\n";
                }
            }
        }

        std::string net_payload = "{\"iface_id\": \"eth0\", \"guest_mac\": \"" + vm.mac + "\", \"host_dev_name\": \"" + vm.tap_name + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/network-interfaces/eth0", net_payload)) { terminate_vm(id); return false; }

        std::string machine_payload = "{\"vcpu_count\": " + std::to_string(vm.vcpus) + ", \"mem_size_mib\": " + std::to_string(vm.mem_mib) + "}";
        if (!send_firecracker_put(vm.socket_path, "/machine-config", machine_payload)) { terminate_vm(id); return false; }

        if (!send_firecracker_put(vm.socket_path, "/actions", "{\"action_type\": \"InstanceStart\"}")) { terminate_vm(id); return false; }

        { std::lock_guard<std::mutex> lock(mtx); vms[id].status = "running"; db.update_status(id, "running"); }
        return true;
    }


    bool reboot_vm(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM old_vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            if (vms[id].pid > 0) {
                vms[id].status = "restarting"; // Tell Reaper NOT to delete disk/TAP
                kill(vms[id].pid, SIGKILL);
            }
            old_vm = vms[id];
        }

        // Wait for process to die and Reaper to clear PID
        for(int i=0; i<50; ++i) {
            bool dead = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (vms.find(id) == vms.end() || vms[id].pid == 0) dead = true;
            }
            if (dead) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Now respawn Firecracker reusing the same TAP and Rootfs!
        {
            std::lock_guard<std::mutex> lock(mtx);
            old_vm.status = "booting";
            unlink(old_vm.socket_path.c_str()); // CRITICAL: remove old UDS so new Firecracker can bind
            
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                int dev_null = open("/dev/null", O_RDWR);
                dup2(dev_null, STDOUT_FILENO); dup2(dev_null, STDERR_FILENO); dup2(dev_null, STDIN_FILENO); close(dev_null);
                execlp("firecracker", "firecracker", "--api-sock", old_vm.socket_path.c_str(), (char*)NULL);
                _exit(1);
            }

            old_vm.pid = pid;
            vms[id] = old_vm;
            pid_to_slot[pid] = id;
            db.update_status(id, "booting");
        }

        // Configure and start!
        return configure_and_start(id, kernel, rootfs);
    }

    void terminate_vm(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) == vms.end()) return;
        
        if (vms[id].pid > 0) {
            kill(vms[id].pid, SIGKILL);
        } else if (vms[id].status == "hibernated") {
            cleanup_vm_resources(vms[id]);
            active_uuids.erase(vms[id].uuid); vms.erase(id);
        }
    }

    // Talks to the Firecracker API to pause the microVM and dump its state.
bool hibernate_vm(int id) {
        std::string vm_uuid = vms[id].uuid;
        std::string snap_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_state.snap";
        std::string mem_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_mem.ram";
        
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
            kill(vms[id].pid, SIGKILL);
        }
        return true;
    }

    void update_vm_ports(int id, bool ssh, bool http, bool https) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) {
            vms[id].ssh_exposed = ssh;
            vms[id].http_exposed = http;
            vms[id].https_exposed = https;
            db.update_vm_ports(id, ssh, http, https);
        }
    }

    // Restores the microVM from the dumped state snapshot. Fast boot magic!
bool wake_vm(int id) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status != "hibernated") return false;
            vms[id].status = "waking"; // Prevent race condition from concurrent Ghost Proxy threads!
            db.update_status(id, "waking");
            vm = vms[id];
        }

        std::string vm_uuid = vms[id].uuid;
        std::string snap_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_state.snap";
        std::string mem_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_mem.ram";

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

    // --- Volumes ---
    int create_volume(const std::string& name, int size_gb) {
        int vol_id = db.insert_volume(name, size_gb);
        std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol_id) + ".ext4";
        
        std::string cmd1 = "truncate -s " + std::to_string(size_gb) + "G \"" + vol_path + "\"";
        std::string cmd2 = "mkfs.ext4 -F \"" + vol_path + "\" > /dev/null 2>&1";
        
        system(cmd1.c_str());
        system(cmd2.c_str());
        
        return vol_id;
    }

    void delete_volume(int vol_id) {
        std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol_id) + ".ext4";
        unlink(vol_path.c_str());
        db.remove_volume(vol_id);
    }

    std::vector<MicroVM> list_vms(int user_id = 1, bool is_admin = true) {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<MicroVM> list;
        for (const auto& pair : vms) {
            if (is_admin || pair.second.owner_id == user_id) {
                list.push_back(pair.second);
            }
        }
        return list;
    }


    bool user_owns_vm(int id, int user_id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) == vms.end()) return false;
        return vms[id].owner_id == user_id;
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

