#include <iostream>
#include <filesystem>
#include <thread>
#include <csignal>
#include "lib/httplib.h"
#include "lib/json.hpp"
#include "config.hpp"
#include "orchestrator.hpp"
#include "proxy.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

static httplib::Server* global_svr = nullptr;

void signal_handler(int sig) {
    std::cout << "\n[*] CryoSpawn Daemon stopping (Signal " << sig << ")...\n";
    if (global_svr) {
        global_svr->stop();
    }
}

int main(int argc, char* argv[]) {
    // Ignore SIGPIPE to prevent unexpected client disconnects from terminating the daemon
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string KERNEL_PATH = config::get_kernel_path();
    std::string ROOTFS_PATH = config::get_rootfs_path();
    int BACKEND_PORT = config::get_backend_port();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--kernel" && i + 1 < argc) KERNEL_PATH = argv[++i];
        else if (arg == "--rootfs" && i + 1 < argc) ROOTFS_PATH = argv[++i];
        else if (arg == "--port" && i + 1 < argc) {
            try { BACKEND_PORT = std::stoi(argv[++i]); } catch (...) {}
        }
    }

    std::cout << "[*] CryoSpawn Daemon Configuration:\n"
              << "    Kernel:  " << KERNEL_PATH << "\n"
              << "    RootFS:  " << ROOTFS_PATH << "\n"
              << "    BaseDir: " << config::BASE_PATH << "\n"
              << "    DB Path: " << config::get_db_path() << "\n"
              << "    Port:    " << BACKEND_PORT << "\n";

    VMManager manager;
    httplib::Server svr;
    global_svr = &svr;

    // Callbacks for Ghost Proxies
    auto wake_fn = [&manager, KERNEL_PATH, ROOTFS_PATH](int vid) {
        return manager.start_or_resume_vm(vid, KERNEL_PATH, ROOTFS_PATH);
    };

    auto status_fn = [&manager](int vid) {
        for (const auto& v : manager.list_vms(1, true)) {
            if (v.id == vid && (v.status == "hibernated" || v.status == "stopped")) return true;
        }
        return false;
    };

    auto sync_vm_proxies = [&](const MicroVM& vm) {
        std::string hex = manager.get_hex(vm.uuid);
        std::string uds_ssh = "/tmp/cryo-" + hex + "_22.sock";
        std::string uds_http = "/tmp/cryo-" + hex + "_80.sock";
        std::string uds_https = "/tmp/cryo-" + hex + "_443.sock";

        // SSH
        if (vm.ssh_exposed) {
            if (!is_ghost_proxy_running(uds_ssh)) {
                std::thread(run_ghost_proxy, uds_ssh, vm.ip, 22, vm.id, wake_fn, status_fn, vm.id).detach();
            }
            if (vm.host_port > 0) {
                std::string tcp_key = "tcp:" + std::to_string(vm.host_port);
                if (!is_ghost_proxy_running(tcp_key)) {
                    std::thread(run_ghost_proxy_tcp, vm.host_port, vm.ip, 22, vm.id, wake_fn, status_fn).detach();
                }
            }
        } else {
            if (is_ghost_proxy_running(uds_ssh)) stop_ghost_proxy(uds_ssh);
            if (vm.host_port > 0) stop_ghost_proxy("tcp:" + std::to_string(vm.host_port));
        }

        // HTTP
        if (vm.http_exposed) {
            if (!is_ghost_proxy_running(uds_http)) {
                std::thread(run_ghost_proxy, uds_http, vm.ip, 80, vm.id, wake_fn, status_fn, vm.id).detach();
            }
        } else {
            if (is_ghost_proxy_running(uds_http)) stop_ghost_proxy(uds_http);
        }

        // HTTPS
        if (vm.https_exposed) {
            if (!is_ghost_proxy_running(uds_https)) {
                std::thread(run_ghost_proxy, uds_https, vm.ip, 443, vm.id, wake_fn, status_fn, vm.id).detach();
            }
        } else {
            if (is_ghost_proxy_running(uds_https)) stop_ghost_proxy(uds_https);
        }
    };

    // Restore Ghost Proxies for all adopted VMs on startup
    for (const auto& vm : manager.list_vms(1, true)) {
        sync_vm_proxies(vm);
    }

    // Helper lambda for authenticating requests
    auto authenticate = [](const httplib::Request& req, httplib::Response& res, int& user_id, bool& is_admin) -> bool {
        if (!req.has_header("X-User-ID")) {
            res.status = 401;
            res.set_content(R"({"error": "Unauthorized. Missing X-User-ID header."})", "application/json");
            return false;
        }
        try {
            user_id = std::stoi(req.get_header_value("X-User-ID"));
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid X-User-ID header."})", "application/json");
            return false;
        }
        is_admin = (req.has_header("X-Role") && req.get_header_value("X-Role") == "admin");
        return true;
    };

    // Returns all active VMs accessible to user
    svr.Get("/api/vms", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        std::string lan_ip = get_lan_ip(manager.active_iface);
        json response_json = json::array();
        for (const auto& vm : manager.list_vms(user_id, is_admin)) {
            response_json.push_back({
                {"id", vm.id},
                {"uuid", vm.uuid},
                {"name", vm.name},
                {"ip", vm.ip},
                {"host_ip", vm.host_ip},
                {"status", vm.status},
                {"vcpus", vm.vcpus},
                {"mem_mib", vm.mem_mib},
                {"tap_name", vm.tap_name},
                {"rootfs_gb", vm.rootfs_gb},
                {"ssh_exposed", vm.ssh_exposed},
                {"host_port", vm.host_port},
                {"http_exposed", vm.http_exposed},
                {"https_exposed", vm.https_exposed},
                {"host_lan_ip", lan_ip},
                {"project", vm.project_name},
                {"owner_id", vm.owner_id}
            });
        }
        res.set_content(response_json.dump(), "application/json");
    });

    // Spawns a brand new microVM
    svr.Post("/api/vms", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        int vcpus = 1; int mem_mib = 512; 
        bool expose_ssh = false; bool expose_http = false; bool expose_https = false;
        int rootfs_gb = 3;
        std::string project = "default"; std::string name = "";

        if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body);
                if (body.contains("vcpus")) vcpus = body["vcpus"];
                if (body.contains("mem_mib")) mem_mib = body["mem_mib"];
                if (body.contains("expose_ssh")) expose_ssh = body["expose_ssh"];
                if (body.contains("expose_http")) expose_http = body["expose_http"];
                if (body.contains("expose_https")) expose_https = body["expose_https"];
                if (body.contains("project")) project = body["project"];
                if (body.contains("name")) name = body["name"];
                if (body.contains("rootfs_gb")) rootfs_gb = body["rootfs_gb"];
                if (rootfs_gb < 3) rootfs_gb = 3;
            } catch (...) {
                res.status = 400; res.set_content(R"({"error": "Invalid JSON"})", "application/json"); return;
            }
        }
        try {
            MicroVM vm = manager.create_vm(vcpus, mem_mib, expose_ssh, expose_http, expose_https, project, rootfs_gb, name, user_id);
            std::thread([&manager, vm, KERNEL_PATH, ROOTFS_PATH]() {
                manager.configure_and_start(vm.id, KERNEL_PATH, ROOTFS_PATH);
            }).detach();

            sync_vm_proxies(vm);

            json response = {
                {"id", vm.id},
                {"uuid", vm.uuid},
                {"name", vm.name},
                {"ip", vm.ip},
                {"host_port", vm.host_port},
                {"status", "booting"}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/restart$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        manager.reboot_vm(id, KERNEL_PATH, ROOTFS_PATH);
        res.set_content(R"({"status": "restarting"})", "application/json");
    });

    // Dynamically opens or closes SSH/HTTP/HTTPS ports
    svr.Patch(R"(^/api/vms/([a-zA-Z0-9-]+)/ports$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        try {
            auto body = json::parse(req.body);
            bool ssh = body.value("ssh_exposed", false);
            bool http = body.value("http_exposed", false);
            bool https = body.value("https_exposed", false);

            manager.update_vm_ports(id, ssh, http, https);

            MicroVM target_vm;
            bool found = false;
            for (const auto& v : manager.list_vms(1, true)) {
                if (v.id == id) {
                    target_vm = v;
                    found = true;
                    break;
                }
            }

            if (!found) {
                res.status = 404;
                res.set_content(R"({"error": "VM not found"})", "application/json");
                return;
            }

            sync_vm_proxies(target_vm);
            res.set_content(R"({"status": "ports updated"})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // Nukes a VM from existence
    svr.Delete(R"(^/api/vms/([a-zA-Z0-9-]+)$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; } 
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        manager.terminate_vm(id);
        res.set_content(R"({"status": "deleted"})", "application/json");
    });

    // Freezes VM to disk (dumps RAM & CPU snapshot)
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/hibernate$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        if (manager.hibernate_vm(id)) {
            res.set_content(R"({"status": "hibernated"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to hibernate"})", "application/json");
        }
    });

    // Thaws or boots a microVM (wake & start unified)
    auto handle_wake_or_start = [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        if (manager.start_or_resume_vm(id, KERNEL_PATH, ROOTFS_PATH)) {
            res.set_content(R"({"status": "running"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to start or wake VM"})", "application/json");
        }
    };

    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/wake$)", handle_wake_or_start);
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/start$)", handle_wake_or_start);

    // Dynamic ghost proxy creation
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/proxy$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        try {
            auto body = json::parse(req.body);
            int host_port = body.value("host_port", 0);
            if (!body.contains("vm_port")) {
                res.status = 400;
                res.set_content(R"({"error": "Missing vm_port in request body"})", "application/json");
                return;
            }
            int vm_port = body["vm_port"];

            if (vm_port < 1 || vm_port > 65535 || (host_port != 0 && (host_port < 1 || host_port > 65535))) {
                res.status = 400;
                res.set_content(R"({"error": "Port numbers must be between 1 and 65535."})", "application/json");
                return;
            }

            std::string vm_ip = "";
            std::string vm_uuid = "";
            for (const auto& v : manager.list_vms(1, true)) {
                if (v.id == id) {
                    vm_ip = v.ip;
                    vm_uuid = v.uuid;
                }
            }

            if (vm_ip.empty()) {
                res.status = 404;
                res.set_content(R"({"error": "VM not found"})", "application/json");
                return;
            }

            std::string uds_path = "/tmp/cryo-" + manager.get_hex(vm_uuid) + "_" + std::to_string(vm_port) + ".sock";
            if (!is_ghost_proxy_running(uds_path)) {
                std::thread(run_ghost_proxy, uds_path, vm_ip, vm_port, id, wake_fn, status_fn, id).detach();
            }

            if (host_port > 0) {
                std::string tcp_key = "tcp:" + std::to_string(host_port);
                if (!is_ghost_proxy_running(tcp_key)) {
                    std::thread(run_ghost_proxy_tcp, host_port, vm_ip, vm_port, id, wake_fn, status_fn).detach();
                }
            }

            json resp = {
                {"status", "proxy_started"},
                {"uds", uds_path},
                {"vm_port", vm_port}
            };
            if (host_port > 0) resp["host_port"] = host_port;
            res.set_content(resp.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", std::string("Invalid request: ") + e.what()}}.dump(), "application/json");
        }
    });

    // --- NETWORK TOPOLOGY API ---
    svr.Get("/api/links", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        json response = json::array();
        auto vms = manager.list_vms(1, true);
        auto get_uuid = [&](int id) {
            for (const auto& v : vms) if (v.id == id) return v.uuid;
            return std::string("");
        };
        for (const auto& link : manager.get_all_links(user_id, is_admin)) {
            response.push_back({{"vm1_uuid", get_uuid(link.vm1_id)}, {"vm2_uuid", get_uuid(link.vm2_id)}});
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/links", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        try {
            auto body = json::parse(req.body);
            std::string vm1_uuid = body.value("vm1_id", "");
            std::string vm2_uuid = body.value("vm2_id", "");
            if (vm1_uuid.empty() || vm2_uuid.empty() || vm1_uuid == vm2_uuid) {
                res.status = 400;
                res.set_content(json{{"error", "Please provide two different, valid VM UUIDs."}}.dump(), "application/json");
                return;
            }
            int id1 = manager.get_slot_by_uuid(vm1_uuid);
            int id2 = manager.get_slot_by_uuid(vm2_uuid);
            if (id1 == -1 || id2 == -1) {
                res.status = 404;
                res.set_content(json{{"error", "One or both VMs not found."}}.dump(), "application/json");
                return;
            }
            if (!manager.can_user_link(id1, id2, user_id, is_admin)) {
                res.status = 403;
                res.set_content(json{{"error", "Permission denied. You can only link your own VMs."}}.dump(), "application/json");
                return;
            }
            if (manager.link_microvms(id1, id2, user_id)) {
                res.set_content(json{{"status", "linked"}}.dump(), "application/json");
            } else {
                res.status = 400; 
                res.set_content(json{{"error", "Cross-project linking is denied. Both VMs must belong to the same project group."}}.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(^/api/links/([a-zA-Z0-9-]+)/([a-zA-Z0-9-]+)$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        int id1 = manager.get_slot_by_uuid(req.matches[1]);
        int id2 = manager.get_slot_by_uuid(req.matches[2]);
        if (id1 == -1 || id2 == -1) {
            res.status = 404;
            res.set_content(json{{"error", "One or both VMs not found."}}.dump(), "application/json");
            return;
        }
        if (!manager.can_user_link(id1, id2, user_id, is_admin)) {
            res.status = 403;
            res.set_content(json{{"error", "Permission denied."}}.dump(), "application/json");
            return;
        }
        manager.unlink_microvms(id1, id2);
        res.set_content(json{{"status", "unlinked"}}.dump(), "application/json");
    });

    // --- VOLUMES API ---
    svr.Get("/api/volumes", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        auto vms = manager.list_vms(1, true);
        auto get_vm_info = [&](int id, std::string& uuid, std::string& name) {
            for (const auto& v : vms) {
                if (v.id == id) {
                    uuid = v.uuid;
                    name = v.name;
                    return;
                }
            }
            uuid = "";
            name = "";
        };

        json response = json::array();
        for (const auto& vol : manager.db.get_volumes(user_id, is_admin)) {
            std::string vm_uuid = "", vm_name = "";
            if (vol.attached_vm_id != -1) {
                get_vm_info(vol.attached_vm_id, vm_uuid, vm_name);
            }
            response.push_back({
                {"id", vol.id},
                {"name", vol.name},
                {"size_gb", vol.size_gb},
                {"attached_vm_id", vol.attached_vm_id},
                {"attached_vm_uuid", vm_uuid},
                {"attached_vm_name", vm_name},
                {"owner_id", vol.owner_id}
            });
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/volumes", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            int size_gb = body.value("size_gb", 1);
            if (name.empty() || size_gb < 1 || size_gb > 1000) {
                res.status = 400;
                res.set_content(json{{"error", "Invalid volume name or size (1-1000 GB)."}}.dump(), "application/json");
                return;
            }
            int id = manager.create_volume(name, size_gb, user_id);
            res.set_content(json{{"status", "created", "id", id}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(^/api/volumes/(\d+)$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        int vol_id = std::stoi(req.matches[1]);
        Database::VolumeRow vol;
        if (!manager.db.get_volume(vol_id, vol)) {
            res.status = 404;
            res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
            return;
        }
        if (!is_admin && vol.owner_id != user_id) {
            res.status = 403;
            res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
            return;
        }
        if (vol.attached_vm_id != -1) {
            res.status = 400;
            res.set_content(json{{"error", "Cannot delete an attached volume. Please detach it first."}}.dump(), "application/json");
            return;
        }
        manager.delete_volume(vol_id);
        res.set_content(json{{"status", "deleted"}}.dump(), "application/json");
    });

    svr.Post(R"(^/api/volumes/(\d+)/attach$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        try {
            int vol_id = std::stoi(req.matches[1]);
            Database::VolumeRow vol;
            if (!manager.db.get_volume(vol_id, vol)) {
                res.status = 404;
                res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
                return;
            }
            if (!is_admin && vol.owner_id != user_id) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
                return;
            }
            if (vol.attached_vm_id != -1) {
                res.status = 400;
                res.set_content(json{{"error", "Volume is already attached to a VM. Please detach it first."}}.dump(), "application/json");
                return;
            }

            auto body = json::parse(req.body);
            std::string vm_uuid = body.value("vm_id", "");
            int vm_id = manager.get_slot_by_uuid(vm_uuid);
            if (vm_id == -1) {
                res.status = 400;
                res.set_content(json{{"error", "Invalid or missing VM UUID."}}.dump(), "application/json");
                return;
            }
            if (!is_admin && !manager.user_owns_vm(vm_id, user_id)) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own the target VM."}}.dump(), "application/json");
                return;
            }

            manager.db.attach_volume(vol_id, vm_id);
            
            // Check if VM is running or hibernated
            for (const auto& v : manager.list_vms(1, true)) {
                if (v.id == vm_id) {
                    if (v.status == "running") {
                        manager.reboot_vm(vm_id, KERNEL_PATH, ROOTFS_PATH);
                    } else if (v.status == "hibernated") {
                        // Invalidate snapshot so the new volume is properly configured on next cold boot
                        std::string snap = config::BASE_PATH + "/instances/cryo-" + manager.get_hex(v.uuid) + "_state.snap";
                        std::string mem = config::BASE_PATH + "/instances/cryo-" + manager.get_hex(v.uuid) + "_mem.ram";
                        std::error_code ec;
                        std::filesystem::remove(snap, ec);
                        std::filesystem::remove(mem, ec);
                        manager.set_vm_status(vm_id, "stopped");
                    }
                    break;
                }
            }
            res.set_content(json{{"status", "attached"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/volumes/(\d+)/detach$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        try {
            int vol_id = std::stoi(req.matches[1]);
            Database::VolumeRow vol;
            if (!manager.db.get_volume(vol_id, vol)) {
                res.status = 404;
                res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
                return;
            }
            if (!is_admin && vol.owner_id != user_id) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
                return;
            }

            int attached_vm = vol.attached_vm_id;
            manager.db.detach_volume(vol_id);
            
            if (attached_vm != -1) {
                for (const auto& v : manager.list_vms(1, true)) {
                    if (v.id == attached_vm) {
                        if (v.status == "running") {
                            manager.reboot_vm(attached_vm, KERNEL_PATH, ROOTFS_PATH);
                        } else if (v.status == "hibernated") {
                            std::string snap = config::BASE_PATH + "/instances/cryo-" + manager.get_hex(v.uuid) + "_state.snap";
                            std::string mem = config::BASE_PATH + "/instances/cryo-" + manager.get_hex(v.uuid) + "_mem.ram";
                            std::error_code ec;
                            std::filesystem::remove(snap, ec);
                            std::filesystem::remove(mem, ec);
                            manager.set_vm_status(attached_vm, "stopped");
                        }
                        break;
                    }
                }
            }
            res.set_content(json{{"status", "detached"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-User-ID, X-Role");
    });
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    std::cout << "[*] CryoSpawn Daemon (C++) pure REST API running on 127.0.0.1:" << BACKEND_PORT << "\n";
    svr.listen("127.0.0.1", BACKEND_PORT);
    return 0;
}