#include <iostream>
#include <filesystem>
#include <thread>
#include "lib/httplib.h"
#include "lib/json.hpp"
#include "config.hpp"
#include "orchestrator.hpp"
#include "proxy.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

int main() {
    std::string KERNEL_PATH = config::BASE_PATH + "/vmlinux/ubuntu-vmlinux.bin";
    std::string ROOTFS_PATH = config::BASE_PATH + "/rootfs/ubuntu-rootfs.ext4";

    VMManager manager;
    httplib::Server svr;

    // Define wake and status functions for the proxies
    auto wake_fn = [&manager](int vid) { return manager.wake_vm(vid); };
    auto status_fn = [&manager](int vid) {
        for (auto& v : manager.list_vms()) {
            if (v.id == vid && v.status == "hibernated") return true;
        }
        return false;
    };

    // Restore Ghost Proxies for all adopted VMs on startup
    for (const auto& vm : manager.list_vms()) {
        if (vm.ssh_exposed) {
            std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_22.sock", vm.ip, 22, vm.id, wake_fn, status_fn).detach();
        }
        if (vm.http_exposed) {
            std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_80.sock", vm.ip, 80, vm.id, wake_fn, status_fn).detach();
        }
        if (vm.https_exposed) {
            std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_443.sock", vm.ip, 443, vm.id, wake_fn, status_fn).detach();
        }
    }

        // Helper lambda for authenticating Python proxy requests
    auto authenticate = [](const httplib::Request& req, httplib::Response& res, int& user_id, bool& is_admin) -> bool {
        if (!req.has_header("X-User-ID")) {
            res.status = 401;
            res.set_content(R"({"error": "Unauthorized. Missing X-User-ID from Python."})", "application/json");
            return false;
        }
        user_id = std::stoi(req.get_header_value("X-User-ID"));
        is_admin = (req.has_header("X-Role") && req.get_header_value("X-Role") == "admin");
        return true;
    };

    // Returns all active VMs. The dashboard calls this every 2 seconds to refresh the UI.
svr.Get("/api/vms", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;

        std::string lan_ip = get_lan_ip(manager.active_iface);
        json response_json = json::array();
        for (const auto& vm : manager.list_vms(user_id, is_admin)) {
            response_json.push_back({
                {"id", vm.id},
                {"uuid", vm.uuid},
                {"name", vm.name}, {"ip", vm.ip}, {"host_ip", vm.host_ip}, {"status", vm.status},
                {"vcpus", vm.vcpus}, {"mem_mib", vm.mem_mib}, {"tap_name", vm.tap_name}, {"rootfs_gb", vm.rootfs_gb},
                {"ssh_exposed", vm.ssh_exposed}, {"host_port", vm.host_port},
                {"http_exposed", vm.http_exposed}, {"https_exposed", vm.https_exposed},
                {"host_lan_ip", lan_ip}, {"project", vm.project_name}
            });
        }
        res.set_content(response_json.dump(), "application/json");
    });

    // Spawns a brand new microVM! Called when you fill out the "Create VM" form.
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
                if (body.contains("rootfs_gb")) { rootfs_gb = body["rootfs_gb"]; }
            } catch (...) {
                res.status = 400; res.set_content(R"({"error": "Invalid JSON"})", "application/json"); return;
            }
        }
        try {
            MicroVM vm = manager.create_vm(vcpus, mem_mib, expose_ssh, expose_http, expose_https, project, rootfs_gb, name);
            std::thread([&manager, vm, KERNEL_PATH, ROOTFS_PATH]() {
                manager.configure_and_start(vm.id, KERNEL_PATH, ROOTFS_PATH);
            }).detach();

            auto wake_fn = [&manager](int vid) { return manager.wake_vm(vid); };
            auto status_fn = [&manager](int vid) {
                for (auto& v : manager.list_vms()) {
                    if (v.id == vid && v.status == "hibernated") return true;
                }
                return false;
            };

            // Wire the Ghost Proxies
            // Wire the Ghost Proxies using Unix Domain Sockets
            if (vm.ssh_exposed) {
                std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_22.sock", vm.ip, 22, vm.id, wake_fn, status_fn).detach();
            }
            if (vm.http_exposed) {
                std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_80.sock", vm.ip, 80, vm.id, wake_fn, status_fn).detach();
            }
            if (vm.https_exposed) {
                std::thread(run_ghost_proxy, "/tmp/cryo-" + manager.get_hex(vm.uuid) + "_443.sock", vm.ip, 443, vm.id, wake_fn, status_fn).detach();
            }

            json response = { {"id", vm.id},
                {"uuid", vm.uuid},
                {"name", vm.name}, {"ip", vm.ip}, {"status", "booting"} };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/restart$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; return; }

        manager.reboot_vm(id, KERNEL_PATH, ROOTFS_PATH);
        res.set_content(R"({"status": "restarting"})", "application/json");
    });

    // Dynamically opens or closes SSH/HTTP ports by spinning up/down Ghost Proxies.
svr.Patch(R"(^/api/vms/([a-zA-Z0-9-]+)/ports$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; return; }

        try {
            auto body = json::parse(req.body);
            bool ssh = body.value("ssh_exposed", false);
            bool http = body.value("http_exposed", false);
            bool https = body.value("https_exposed", false);

            manager.update_vm_ports(id, ssh, http, https);

            std::string vm_ip = "";
            for (auto& v : manager.list_vms()) {
                if (v.id == id) vm_ip = v.ip;
            }

            if (vm_ip.empty()) {
                res.status = 404;
                res.set_content(R"({"error": "VM not found"})", "application/json");
                return;
            }

            auto wake_fn = [&manager](int vid) { return manager.wake_vm(vid); };
            auto status_fn = [&manager](int vid) {
                for (auto& v : manager.list_vms()) {
                    if (v.id == vid && v.status == "hibernated") return true;
                }
                return false;
            };

            std::string vm_uuid = "";
            for (auto& v : manager.list_vms()) if (v.id == id) vm_uuid = v.uuid;
            std::string uds_ssh = "/tmp/cryo-" + manager.get_hex(vm_uuid) + "_22.sock";
            std::string uds_http = "/tmp/cryo-" + manager.get_hex(vm_uuid) + "_80.sock";
            std::string uds_https = "/tmp/cryo-" + manager.get_hex(vm_uuid) + "_443.sock";

            if (ssh && !is_ghost_proxy_running(uds_ssh)) {
                std::thread(run_ghost_proxy, uds_ssh, vm_ip, 22, id, wake_fn, status_fn).detach();
            } else if (!ssh && is_ghost_proxy_running(uds_ssh)) {
                stop_ghost_proxy(uds_ssh);
            }

            if (http && !is_ghost_proxy_running(uds_http)) {
                std::thread(run_ghost_proxy, uds_http, vm_ip, 80, id, wake_fn, status_fn).detach();
            } else if (!http && is_ghost_proxy_running(uds_http)) {
                stop_ghost_proxy(uds_http);
            }

            if (https && !is_ghost_proxy_running(uds_https)) {
                std::thread(run_ghost_proxy, uds_https, vm_ip, 443, id, wake_fn, status_fn).detach();
            } else if (!https && is_ghost_proxy_running(uds_https)) {
                stop_ghost_proxy(uds_https);
            }

            res.set_content(R"({"status": "ports updated"})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // Nukes a VM from existence. Deletes the disk and kills the Ghost Proxy.
svr.Delete(R"(^/api/vms/([a-zA-Z0-9-]+)$)", [&](const httplib::Request& req, httplib::Response& res) {
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; } 
        manager.terminate_vm(id);
        manager.db.remove_vm(id);
        res.set_content(R"({"status": "deleted"})", "application/json");
    });

    // Freezes the VM to disk. Saves RAM into a snapshot file.
svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/hibernate$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; return; }

        if (manager.hibernate_vm(id)) {
            res.set_content(R"({"status": "hibernated"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to hibernate"})", "application/json");
        }
    });

    // Thaws a frozen VM back into RAM. Resumes execution instantly.
svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/wake$)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; return; }

        if (manager.wake_vm(id)) {
            res.set_content(R"({"status": "running"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to wake VM"})", "application/json");
        }
    });

    svr.Post(R"(/api/vms/([a-zA-Z0-9-]+)/proxy)", [&](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!authenticate(req, res, user_id, is_admin)) return;
        
        int id = manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; return; }
        if (!is_admin && !manager.user_owns_vm(id, user_id)) { res.status = 403; return; }

        try {
            auto body = json::parse(req.body);
            int host_port = body["host_port"];
            int vm_port = body["vm_port"];

            std::string vm_ip = "";
            for (auto& v : manager.list_vms()) {
                if (v.id == id) vm_ip = v.ip;
            }

            if (vm_ip.empty()) {
                res.status = 404;
                res.set_content(R"({"error": "VM not found"})", "application/json");
                return;
            }

            auto wake_fn = [&manager](int vid) { return manager.wake_vm(vid); };
            auto status_fn = [&manager](int vid) {
                for (auto& v : manager.list_vms()) {
                    if (v.id == vid && v.status == "hibernated") return true;
                }
                return false;
            };

            std::string uds_path = "/tmp/cryo_vm_" + std::to_string(id) + "_" + std::to_string(vm_port) + ".sock";
            std::thread(run_ghost_proxy, uds_path, vm_ip, vm_port, id, wake_fn, status_fn).detach();
            res.set_content(R"({"status": "proxy_started", "uds": ")" + uds_path + R"("})", "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid request"})", "application/json");
        }
    });

    // --- NETWORK TOPOLOGY API ---
    svr.Get("/api/links", [&](const httplib::Request& req, httplib::Response& res) {
        json response = json::array();
        auto vms = manager.list_vms();
        auto get_uuid = [&](int id) {
            for (auto& v : vms) if (v.id == id) return v.uuid;
            return std::string("");
        };
        for (const auto& link : manager.get_all_links()) {
            response.push_back({{"vm1_uuid", get_uuid(link.vm1_id)}, {"vm2_uuid", get_uuid(link.vm2_id)}});
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/links", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            if (manager.link_microvms(manager.get_slot_by_uuid(body["vm1_id"]), manager.get_slot_by_uuid(body["vm2_id"]))) {
                res.set_content(json{{"status", "linked"}}.dump(), "application/json");
            } else {
                res.status = 400; 
                res.set_content(json{{"error", "Cross-project linking is denied."}}.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/links/([a-zA-Z0-9-]+)/([a-zA-Z0-9-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        manager.unlink_microvms(manager.get_slot_by_uuid(req.matches[1]), manager.get_slot_by_uuid(req.matches[2]));
        res.set_content(json{{"status", "unlinked"}}.dump(), "application/json");
    });

    // --- VOLUMES API ---
    svr.Get("/api/volumes", [&](const httplib::Request& req, httplib::Response& res) {
        json response = json::array();
        for (const auto& vol : manager.db.get_volumes()) {
            response.push_back({
                {"id", vol.id},
                {"name", vol.name},
                {"size_gb", vol.size_gb},
                {"attached_vm_id", vol.attached_vm_id}
            });
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/volumes", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            int id = manager.create_volume(body["name"], body["size_gb"]);
            res.set_content(json{{"status", "created", "id", id}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(^/api/volumes/(\d+)$)", [&](const httplib::Request& req, httplib::Response& res) {
        manager.delete_volume(std::stoi(req.matches[1]));
        res.set_content(json{{"status", "deleted"}}.dump(), "application/json");
    });

    svr.Post(R"(^/api/volumes/(\d+)/attach$)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int vol_id = std::stoi(req.matches[1]);
            auto body = json::parse(req.body);
            int vm_id = manager.get_slot_by_uuid(body["vm_id"]);
            
            manager.db.attach_volume(vol_id, vm_id);
            
            // Check if VM is running, reboot if necessary
            for (auto& v : manager.list_vms()) {
                if (v.id == vm_id && v.status == "running") {
                    manager.reboot_vm(vm_id, KERNEL_PATH, ROOTFS_PATH);
                    
                    break;
                }
            }
            res.set_content(json{{"status", "attached"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/volumes/(\d+)/detach$)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int vol_id = std::stoi(req.matches[1]);
            int attached_vm = -1;
            for (const auto& vol : manager.db.get_volumes()) {
                if (vol.id == vol_id) attached_vm = vol.attached_vm_id;
            }
            manager.db.detach_volume(vol_id);
            
            if (attached_vm != -1) {
                for (auto& v : manager.list_vms()) {
                    if (v.id == attached_vm && v.status == "running") {
                        manager.reboot_vm(attached_vm, KERNEL_PATH, ROOTFS_PATH);
                        
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    // The C++ daemon is now a pure REST API on port 9090.
    // The Flask Python frontend on port 8080 will handle all UI and proxying.
    const int BACKEND_PORT = 9090;
    
    std::cout << "[*] CryoSpawn Daemon (C++) pure REST API running on 127.0.0.1:" << BACKEND_PORT << "\n";
    svr.listen("127.0.0.1", BACKEND_PORT);
    return 0;
}