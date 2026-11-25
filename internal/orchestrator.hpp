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
#include <chrono>

#include "api.hpp"
#include "network.hpp"
#include "config.hpp"
#include "database.hpp" 
#include "proxy.hpp" 

namespace fs = std::filesystem;

struct MicroVM {
    int owner_id = 1;
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
    int rootfs_gb = 3;
    bool ssh_exposed = false; 
    bool http_exposed = false;
    bool https_exposed = false;
    int host_port = 0;
    std::string project_name = "default";
    bool allow_interproject_routing = false;
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

    static int run_exec(const std::vector<std::string>& args, bool silence_output = false) {
        if (args.empty()) return -1;
        pid_t pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) {
            if (silence_output) {
                int dev_null = open("/dev/null", O_RDWR);
                if (dev_null >= 0) {
                    dup2(dev_null, STDOUT_FILENO);
                    dup2(dev_null, STDERR_FILENO);
                    close(dev_null);
                }
            }
            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& a : args) {
                argv.push_back(const_cast<char*>(a.c_str()));
            }
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    std::vector<std::string> collect_peer_taps_locked(int vm_id) {
        std::vector<std::string> peer_taps;
        auto links = db.get_all_links();
        for (const auto& link : links) {
            if (link.vm1_id == vm_id && vms.count(link.vm2_id)) {
                peer_taps.push_back(vms[link.vm2_id].tap_name);
            } else if (link.vm2_id == vm_id && vms.count(link.vm1_id)) {
                peer_taps.push_back(vms[link.vm1_id].tap_name);
            }
        }
        return peer_taps;
    }

public:
    static std::string generate_mac(int slot) {
        int base = slot * 4;
        int octet3 = (base / 256) & 0xFF;
        int octet4 = ((base % 256) + 2) & 0xFF;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "06:00:AC:10:%02X:%02X", octet3, octet4);
        return std::string(buf);
    }

    Database db;
    std::unordered_set<std::string> active_uuids;
    std::string active_iface;

    std::string get_hex(const std::string& id) {
        if (id.length() > 3 && id.substr(0, 3) == "vm-") return id.substr(3);
        if (id.length() > 4 && id.substr(0, 4) == "vol-") return id.substr(4);
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

    void set_vm_status(int id, const std::string& status) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) {
            vms[id].status = status;
            if (status == "stopped" || status == "hibernated") {
                vms[id].pid = 0;
            }
        }
        db.update_status(id, status);
    }

    // Sweeps up network taps, sockets, disk images, snapshots, and ghost proxies
    void cleanup_vm_resources(const MicroVM& vm, const std::vector<std::string>& peer_taps = {}) {
        std::cout << "[*] Cleaning up resources for VM " << vm.id << " (" << vm.uuid << ")...\n";
        for (const auto& peer_tap : peer_taps) {
            unlink_taps(vm.tap_name, peer_tap);
        }
        teardown_cryo_network(vm.tap_name, active_iface);
        if (vm.ssh_exposed && vm.host_port > 0) {
            unexpose_vm_port(active_iface, vm.host_port, vm.ip, 22);
        }
        unlink(vm.socket_path.c_str());

        // Detach any attached volumes so they are not orphaned
        db.detach_all_volumes_from_vm(vm.id);

        // Delete unique rootfs disk and snapshots
        std::string base_dir = config::BASE_PATH + "/instances";
        std::string vm_rootfs = base_dir + "/cryo-" + get_hex(vm.uuid) + "_rootfs.ext4";
        std::string vm_snap = base_dir + "/cryo-" + get_hex(vm.uuid) + "_state.snap";
        std::string vm_mem = base_dir + "/cryo-" + get_hex(vm.uuid) + "_mem.ram";

        std::error_code ec;
        if (std::filesystem::exists(vm_rootfs, ec)) {
            std::filesystem::remove(vm_rootfs, ec);
            std::cout << "[+] Deleted VM disk: " << vm_rootfs << "\n";
        }
        if (std::filesystem::exists(vm_snap, ec)) {
            std::filesystem::remove(vm_snap, ec);
            std::cout << "[+] Deleted VM state snapshot: " << vm_snap << "\n";
        }
        if (std::filesystem::exists(vm_mem, ec)) {
            std::filesystem::remove(vm_mem, ec);
            std::cout << "[+] Deleted VM RAM snapshot: " << vm_mem << "\n";
        }

        // Clean up Ghost Proxy UDS sockets and TCP listeners
        stop_all_ghost_proxies_for_vm(get_hex(vm.uuid), vm.id, vm.host_port);

        db.remove_vm(vm.id);
        db.cleanup_orphaned_links();
    }

    void reconcile_state() {
        std::cout << "[*] Reconciling state from database...\n";
        std::unordered_set<std::string> active_taps;
        auto records = db.get_all_vms();
        for (const auto& r : records) {
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
            vm.allow_interproject_routing = r.allow_interproject_routing;
            vm.mac = r.mac.empty() ? generate_mac(r.slot) : r.mac;
            if (r.mac.empty()) {
                db.update_mac(vm.id, vm.mac);
            }

            if (vm.uuid.empty()) {
                vm.uuid = generate_vm_id();
                if (vm.name.empty()) vm.name = "vm-" + std::to_string(r.slot);
                db.update_uuid_and_name(vm.id, vm.uuid, vm.name);
            }

            // Hibernated or stopped VMs are preserved intact on disk
            if (r.status == "hibernated" || r.status == "stopped") {
                vm.pid = 0;
                active_uuids.insert(vm.uuid);
                vms[r.slot] = vm;
                std::cout << "[+] Preserved " << r.status << " VM " << r.slot << " (" << vm.uuid << ")\n";
            } else if (r.pid > 0 && kill(r.pid, 0) == 0) {
                // Running VM whose Firecracker process is genuinely active
                active_uuids.insert(vm.uuid);
                vms[r.slot] = vm; 
                pid_to_slot[r.pid] = r.slot;
                active_taps.insert(vm.tap_name);
                std::cout << "[+] Adopted running VM " << r.slot << " (PID " << r.pid << ")\n";
            } else {
                std::cout << "[-] Cleaning up stale DB entry for unrecoverable VM " << r.slot << "\n";
                cleanup_vm_resources(vm);
            }
        }
        db.cleanup_orphaned_volumes();
        db.cleanup_orphaned_links();
        cleanup_orphaned_taps(active_taps);
    }

    void start_reaper() {
        std::thread([this]() {
            while (true) {
                std::vector<std::pair<MicroVM, std::vector<std::string>>> to_cleanup;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    std::vector<pid_t> tracked_pids;
                    tracked_pids.reserve(pid_to_slot.size());
                    for (const auto& kv : pid_to_slot) {
                        tracked_pids.push_back(kv.first);
                    }

                    for (pid_t pid : tracked_pids) {
                        int status = 0;
                        if (waitpid(pid, &status, WNOHANG) > 0) {
                            auto pit = pid_to_slot.find(pid);
                            if (pit == pid_to_slot.end()) continue;
                            int slot = pit->second;
                            auto vit = vms.find(slot);
                            if (vit != vms.end()) {
                                if (vit->second.status == "hibernated" || vit->second.status == "restarting" || vit->second.status == "stopped") {
                                    vit->second.pid = 0;
                                    pid_to_slot.erase(pit);
                                } else {
                                    auto peer_taps = collect_peer_taps_locked(slot);
                                    to_cleanup.push_back({vit->second, std::move(peer_taps)});
                                    active_uuids.erase(vit->second.uuid);
                                    vms.erase(vit);
                                    pid_to_slot.erase(pit);
                                }
                            } else {
                                pid_to_slot.erase(pit);
                            }
                        }
                    }

                    // Check for adopted processes or externally killed processes that exited
                    std::vector<int> dead_slots;
                    for (const auto& pair : vms) {
                        const std::string& st = pair.second.status;
                        if (st == "hibernated" || st == "stopped" || st == "restarting" || st == "booting" || st == "waking" || st == "terminating") {
                            continue;
                        }
                        if (pair.second.pid > 0 && kill(pair.second.pid, 0) != 0) {
                            dead_slots.push_back(pair.first);
                        }
                    }
                    for (int slot : dead_slots) {
                        auto vit = vms.find(slot);
                        if (vit != vms.end()) {
                            pid_to_slot.erase(vit->second.pid);
                            auto peer_taps = collect_peer_taps_locked(slot);
                            to_cleanup.push_back({vit->second, std::move(peer_taps)});
                            active_uuids.erase(vit->second.uuid);
                            vms.erase(vit);
                        }
                    }
                }

                for (const auto& item : to_cleanup) {
                    cleanup_vm_resources(item.first, item.second);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
    }

    VMManager(const std::string& db_path = config::get_db_path()) : db(db_path) {
        active_iface = get_default_interface();
        init_cryo_firewall_baseline(active_iface);
        std::filesystem::create_directories(config::BASE_PATH + "/instances");
        std::filesystem::create_directories(config::BASE_PATH + "/volumes");
        reconcile_state(); 
        start_reaper();
    }

    // Provisions slot, IP, UUID, TAP, and saves metadata in SQLite
    MicroVM create_vm(int vcpus, int mem_mib, bool expose_ssh, bool expose_http, bool expose_https, const std::string& project, int rootfs_gb, const std::string& name, int owner_id = 1, bool allow_interproject_routing = false) {
        std::lock_guard<std::mutex> lock(mtx);
        int slot = db.get_next_free_slot();
        int base = slot * 4;
        
        MicroVM vm;
        vm.id = slot;
        vm.name = name.empty() ? ("vm-" + std::to_string(slot)) : name;
        vm.owner_id = owner_id;
        vm.allow_interproject_routing = allow_interproject_routing;
        do {
            vm.uuid = generate_vm_id();
        } while (active_uuids.find(vm.uuid) != active_uuids.end());
        active_uuids.insert(vm.uuid);

        vm.host_ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 1);
        vm.ip = "172.16." + std::to_string(base / 256) + "." + std::to_string((base % 256) + 2);
        vm.mac = generate_mac(slot);
        vm.tap_name = "cryo-" + get_hex(vm.uuid);
        vm.socket_path = "/tmp/cryo-" + get_hex(vm.uuid) + ".socket";
        vm.vcpus = vcpus;
        vm.mem_mib = mem_mib;
        vm.status = "booting";
        vm.ssh_exposed = expose_ssh; 
        vm.http_exposed = expose_http;
        vm.https_exposed = expose_https;
        vm.project_name = project.empty() ? "default" : project;
        vm.rootfs_gb = (rootfs_gb < 3) ? 3 : rootfs_gb;
        if (expose_ssh) vm.host_port = 2200 + slot;

        setup_cryo_network(vm.tap_name, vm.host_ip, active_iface);

        unlink(vm.socket_path.c_str());
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            int dev_null = open("/dev/null", O_RDWR);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);
            execlp("firecracker", "firecracker", "--api-sock", vm.socket_path.c_str(), (char*)NULL);
            _exit(1);
        }

        vm.pid = pid;
        vms[vm.id] = vm;
        pid_to_slot[pid] = vm.id;
        db.insert_vm(slot, pid, "booting", vcpus, mem_mib, vm.ip, vm.host_ip, vm.tap_name, expose_ssh, vm.host_port, vm.project_name, expose_http, expose_https, vm.rootfs_gb, vm.uuid, vm.name, owner_id, allow_interproject_routing, vm.mac);
        return vm;
    }

    // Configures Firecracker through API socket and boots guest Linux kernel
    bool configure_and_start(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            if (vms[id].mac.empty()) {
                vms[id].mac = generate_mac(id);
                db.update_mac(id, vms[id].mac);
            }
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
        std::string vm_uuid = vm.uuid;
        std::filesystem::path vm_rootfs = std::filesystem::path(config::BASE_PATH + "/instances") / ("cryo-" + get_hex(vm_uuid) + "_rootfs.ext4");
        
        try {
            if (!std::filesystem::exists(vm_rootfs)) {
                std::cout << "[*] Cloning disk image for VM " << id << "...\n";
                std::filesystem::copy(base_path, vm_rootfs);
                if (vm.rootfs_gb >= 3) {
                    std::cout << "[*] Expanding rootfs to " << vm.rootfs_gb << "GB...\n";
                    run_exec({"truncate", "-s", std::to_string(vm.rootfs_gb) + "G", vm_rootfs.string()});
                    run_exec({"e2fsck", "-fy", vm_rootfs.string()}, true);
                    run_exec({"resize2fs", vm_rootfs.string()}, true);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[-] Failed to clone rootfs: " << e.what() << "\n";
            terminate_vm(id);
            return false;
        }

        // 2. Attach instance root disk
        std::string drive_payload = "{\"drive_id\": \"rootfs\", \"path_on_host\": \"" + vm_rootfs.string() + "\", \"is_root_device\": true, \"is_read_only\": false}";
        if (!send_firecracker_put(vm.socket_path, "/drives/rootfs", drive_payload)) { terminate_vm(id); return false; }

        // 3. Attach assigned Elastic Volumes
        auto all_vols = db.get_volumes(1, true);
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

        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status == "terminating") {
                return false;
            }
        }

        if (!send_firecracker_put(vm.socket_path, "/actions", "{\"action_type\": \"InstanceStart\"}")) { terminate_vm(id); return false; }

        { 
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status == "terminating") {
                return false;
            }
            vms[id].status = "running";
            db.update_status(id, "running");
        }
        return true;
    }

    bool reboot_vm(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM old_vm;
        pid_t old_pid = 0;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            if (vms[id].status == "terminating") return false;
            old_pid = vms[id].pid;
            if (old_pid > 0) {
                vms[id].status = "restarting";
                kill(old_pid, SIGKILL);
            }
            old_vm = vms[id];
            if (old_vm.mac.empty()) {
                old_vm.mac = generate_mac(id);
                vms[id].mac = old_vm.mac;
                db.update_mac(id, old_vm.mac);
            }
        }

        // Wait for old process to exit (reap if child, or check exit if adopted)
        if (old_pid > 0) {
            for (int i = 0; i < 30; ++i) {
                int status;
                if (waitpid(old_pid, &status, WNOHANG) > 0 || kill(old_pid, 0) != 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::lock_guard<std::mutex> lock(mtx);
            pid_to_slot.erase(old_pid);
            if (vms.find(id) != vms.end()) {
                vms[id].pid = 0;
            }
        }

        // Respawn Firecracker reusing TAP and Rootfs
        {
            std::lock_guard<std::mutex> lock(mtx);
            old_vm.status = "booting";
            unlink(old_vm.socket_path.c_str());
            
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                int dev_null = open("/dev/null", O_RDWR);
                dup2(dev_null, STDOUT_FILENO);
                dup2(dev_null, STDERR_FILENO);
                dup2(dev_null, STDIN_FILENO);
                close(dev_null);
                execlp("firecracker", "firecracker", "--api-sock", old_vm.socket_path.c_str(), (char*)NULL);
                _exit(1);
            }

            old_vm.pid = pid;
            vms[id] = old_vm;
            pid_to_slot[pid] = id;
            db.update_status(id, "booting");
        }

        return configure_and_start(id, kernel, rootfs);
    }

    void terminate_vm(int id) {
        MicroVM vm_copy;
        std::vector<std::string> peer_taps;
        pid_t pid = 0;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = vms.find(id);
            if (it == vms.end()) return;
            it->second.status = "terminating";
            db.update_status(id, "terminating");
            pid = it->second.pid;
            vm_copy = it->second;
            peer_taps = collect_peer_taps_locked(id);
            if (pid > 0) {
                pid_to_slot.erase(pid);
            }
            active_uuids.erase(it->second.uuid);
            vms.erase(it);
        }

        if (pid > 0) {
            kill(pid, SIGKILL);
            for (int i = 0; i < 25; ++i) {
                int status;
                if (waitpid(pid, &status, WNOHANG) > 0 || kill(pid, 0) != 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        cleanup_vm_resources(vm_copy, peer_taps);
    }

    bool hibernate_vm(int id) {
        std::string vm_uuid;
        std::string socket_path;
        pid_t pid = 0;

        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status != "running") return false;
            vm_uuid = vms[id].uuid;
            socket_path = vms[id].socket_path;
            pid = vms[id].pid;
        }

        std::string snap_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_state.snap";
        std::string mem_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_mem.ram";
        
        // 1. Pause VM execution
        if (!send_firecracker_patch(socket_path, "/vm", "{\"state\": \"Paused\"}")) return false;
        
        // 2. Dump RAM and CPU state to disk
        std::string snap_payload = "{\"snapshot_type\": \"Full\", \"snapshot_path\": \"" + snap_path + "\", \"mem_file_path\": \"" + mem_path + "\"}";
        if (!send_firecracker_put(socket_path, "/snapshot/create", snap_payload)) return false;
        
        // 3. Mark as hibernated BEFORE killing so Reaper leaves disk & TAP intact
        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].status = "hibernated";
            vms[id].pid = 0;
            db.update_status(id, "hibernated");
        }
        
        // 4. Kill the Firecracker process to free host RAM
        if (pid > 0) kill(pid, SIGKILL);
        return true;
    }

    void update_vm_ports(int id, bool ssh, bool http, bool https) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) {
            vms[id].ssh_exposed = ssh;
            vms[id].http_exposed = http;
            vms[id].https_exposed = https;
            if (ssh && vms[id].host_port <= 0) {
                vms[id].host_port = 2200 + id;
            }
            db.update_vm_ports(id, ssh, http, https, vms[id].host_port);
        }
    }

    // Restores microVM from memory snapshot
    bool wake_vm(int id) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end() || vms[id].status != "hibernated") return false;
            vms[id].status = "waking";
            db.update_status(id, "waking");
            vm = vms[id];
        }

        std::string vm_uuid = vm.uuid;
        std::string snap_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_state.snap";
        std::string mem_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm_uuid) + "_mem.ram";

        unlink(vm.socket_path.c_str());
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            int dev_null = open("/dev/null", O_RDWR);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);
            execlp("firecracker", "firecracker", "--api-sock", vm.socket_path.c_str(), (char*)NULL);
            _exit(1);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].pid = pid;
            pid_to_slot[pid] = id;
        }

        bool ready = false;
        for (int i = 0; i < 30; ++i) {
            if (access(vm.socket_path.c_str(), F_OK) == 0) { ready = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) {
            set_vm_status(id, "stopped");
            return false;
        }

        // Load snapshot back into memory
        std::string load_payload = "{\"snapshot_path\": \"" + snap_path + "\", \"mem_file_path\": \"" + mem_path + "\"}";
        if (!send_firecracker_put(vm.socket_path, "/snapshot/load", load_payload)) {
            // If snapshot failed to load, mark stopped instead of destroying the VM
            set_vm_status(id, "stopped");
            return false;
        }

        // Resume VM execution
        if (!send_firecracker_patch(vm.socket_path, "/vm", "{\"state\": \"Resumed\"}")) {
            set_vm_status(id, "stopped");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            vms[id].status = "running";
            db.update_status(id, "running");
        }
        return true;
    }

    // Unified launcher: resumes snapshot if hibernated, or boots cleanly if stopped
    bool start_or_resume_vm(int id, const std::string& kernel, const std::string& rootfs) {
        MicroVM vm;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (vms.find(id) == vms.end()) return false;
            if (vms[id].status == "terminating") return false;
            vm = vms[id];
        }

        if (vm.status == "running") return true;

        std::string snap_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm.uuid) + "_state.snap";
        std::string mem_path = config::BASE_PATH + "/instances/cryo-" + get_hex(vm.uuid) + "_mem.ram";

        std::error_code ec;
        if (vm.status == "hibernated" && std::filesystem::exists(snap_path, ec) && std::filesystem::exists(mem_path, ec)) {
            if (wake_vm(id)) return true;
        }

        // Cold boot fallback for stopped VMs or invalidated snapshots
        return reboot_vm(id, kernel, rootfs);
    }

    // --- Volumes ---
    int create_volume(const std::string& name, int size_gb, int owner_id = 1) {
        int vol_id = db.insert_volume(name, size_gb, owner_id);
        std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol_id) + ".ext4";
        
        run_exec({"truncate", "-s", std::to_string(size_gb) + "G", vol_path});
        run_exec({"mkfs.ext4", "-F", vol_path}, true);
        
        return vol_id;
    }

    void delete_volume(int vol_id) {
        std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol_id) + ".ext4";
        unlink(vol_path.c_str());
        db.remove_volume(vol_id);
    }

    bool delete_volume_if_unattached(int vol_id) {
        if (!db.remove_volume_if_unattached(vol_id)) {
            return false;
        }
        std::string vol_path = config::BASE_PATH + "/volumes/vol_" + std::to_string(vol_id) + ".ext4";
        unlink(vol_path.c_str());
        return true;
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

    bool get_vm(int id, MicroVM& vm) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) {
            vm = vms[id];
            return true;
        }
        return false;
    }

    int get_vm_owner(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) return vms[id].owner_id;
        return -1;
    }

    void update_vm_routing(int id, bool allow_interproject_routing) {
        std::lock_guard<std::mutex> lock(mtx);
        if (vms.find(id) != vms.end()) {
            vms[id].allow_interproject_routing = allow_interproject_routing;
            db.update_vm_routing(id, allow_interproject_routing);

            // If inter-project routing is revoked, proactively sever any active cross-project links
            if (!allow_interproject_routing) {
                auto links = db.get_all_links();
                for (const auto& link : links) {
                    if (link.vm1_id == id || link.vm2_id == id) {
                        int other_id = (link.vm1_id == id) ? link.vm2_id : link.vm1_id;
                        if (vms.find(other_id) != vms.end()) {
                            if (vms[other_id].project_name != vms[id].project_name) {
                                db.remove_link(link.vm1_id, link.vm2_id);
                                unlink_taps(vms[id].tap_name, vms[other_id].tap_name);
                            }
                        } else {
                            db.remove_link(link.vm1_id, link.vm2_id);
                        }
                    }
                }
            }
        }
    }

    bool can_user_link(int id1, int id2, int user_id, bool is_admin) {
        std::lock_guard<std::mutex> lock(mtx);
        if (id1 == id2) return false;
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        if (vms[id1].status == "terminating" || vms[id2].status == "terminating") return false;
        // Cross-user network links remain strictly forbidden; any attempt to bridge VMs belonging to different owners must be rejected with HTTP 403 Forbidden
        if (vms[id1].owner_id != vms[id2].owner_id) return false;
        // Non-admin users cannot link resources owned by other users
        if (!is_admin && vms[id1].owner_id != user_id) return false;
        return true;
    }

    bool can_user_unlink(int id1, int id2, int user_id, bool is_admin) {
        std::lock_guard<std::mutex> lock(mtx);
        if (id1 == id2) return false;
        if (is_admin) return true;
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        if (vms[id1].owner_id != user_id || vms[id2].owner_id != user_id) return false;
        return true;
    }

    bool can_route_interproject(int id1, int id2) {
        std::lock_guard<std::mutex> lock(mtx);
        if (id1 == id2) return false;
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        if (vms[id1].status == "terminating" || vms[id2].status == "terminating") return false;
        if (vms[id1].project_name == vms[id2].project_name) return true;
        return (vms[id1].allow_interproject_routing && vms[id2].allow_interproject_routing);
    }

    bool link_microvms(int id1, int id2, int owner_id = -1) {
        std::lock_guard<std::mutex> lock(mtx);
        if (id1 == id2) return false;
        if (vms.find(id1) == vms.end() || vms.find(id2) == vms.end()) return false;
        if (vms[id1].status == "terminating" || vms[id2].status == "terminating") return false;
        if (vms[id1].owner_id != vms[id2].owner_id) return false;
        bool same_proj = (vms[id1].project_name == vms[id2].project_name);
        if (!same_proj && (!vms[id1].allow_interproject_routing || !vms[id2].allow_interproject_routing)) {
            return false;
        }
        int effective_owner = (owner_id > 0) ? owner_id : vms[id1].owner_id;
        std::string link_proj = same_proj ? vms[id1].project_name : "interproject";
        if (db.add_link(link_proj, id1, id2, effective_owner)) {
            link_taps(vms[id1].tap_name, vms[id2].tap_name);
        }
        return true;
    }

    void add_vm_for_test(const MicroVM& vm) {
        std::lock_guard<std::mutex> lock(mtx);
        vms[vm.id] = vm;
        active_uuids.insert(vm.uuid);
        db.insert_vm(vm.id, vm.pid, vm.status, vm.vcpus, vm.mem_mib, vm.ip, vm.host_ip, vm.tap_name, vm.ssh_exposed, vm.host_port, vm.project_name, vm.http_exposed, vm.https_exposed, vm.rootfs_gb, vm.uuid, vm.name, vm.owner_id, vm.allow_interproject_routing, vm.mac);
    }

    bool unlink_microvms(int id1, int id2) {
        std::lock_guard<std::mutex> lock(mtx);
        if (id1 == id2) return false;
        db.remove_link(id1, id2);
        if (vms.find(id1) != vms.end() && vms.find(id2) != vms.end()) {
            unlink_taps(vms[id1].tap_name, vms[id2].tap_name);
        }
        return true;
    }

    std::vector<Database::LinkRow> get_all_links(int user_id = 1, bool is_admin = true) {
        return db.get_all_links(user_id, is_admin);
    }
};
