#include <iostream>
#include <filesystem>
#include <thread>
#include "httplib.h"
#include "json.hpp"
#include "orchestrator.hpp"
#include "proxy.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

int main() {
    std::string KERNEL_PATH = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/vmlinux/ubuntu-vmlinux.bin";
    std::string ROOTFS_PATH = "/home/nirjhar/Python Codes/Einstein/CryoSpawn/rootfs/ubuntu-rootfs.ext4";

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
            std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_22.sock", vm.ip, 22, vm.id, wake_fn, status_fn).detach();
        }
        if (vm.http_exposed) {
            std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_80.sock", vm.ip, 80, vm.id, wake_fn, status_fn).detach();
        }
        if (vm.https_exposed) {
            std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_443.sock", vm.ip, 443, vm.id, wake_fn, status_fn).detach();
        }
    }

    svr.Get("/api/vms", [&](const httplib::Request& req, httplib::Response& res) {
        std::string lan_ip = get_lan_ip(manager.active_iface);
        json response_json = json::array();
        for (const auto& vm : manager.list_vms()) {
            response_json.push_back({
                {"id", vm.id}, {"ip", vm.ip}, {"host_ip", vm.host_ip}, {"status", vm.status},
                {"vcpus", vm.vcpus}, {"mem_mib", vm.mem_mib}, {"tap_name", vm.tap_name},
                {"ssh_exposed", vm.ssh_exposed}, {"host_port", vm.host_port},
                {"http_exposed", vm.http_exposed}, {"https_exposed", vm.https_exposed},
                {"host_lan_ip", lan_ip}, {"project", vm.project_name}
            });
        }
        res.set_content(response_json.dump(), "application/json");
    });

    svr.Post("/api/vms", [&](const httplib::Request& req, httplib::Response& res) {
        int vcpus = 1; int mem_mib = 512; 
        bool expose_ssh = false; bool expose_http = false; bool expose_https = false;
        std::string project = "default";

        if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body);
                if (body.contains("vcpus")) vcpus = body["vcpus"];
                if (body.contains("mem_mib")) mem_mib = body["mem_mib"];
                if (body.contains("expose_ssh")) expose_ssh = body["expose_ssh"];
                if (body.contains("expose_http")) expose_http = body["expose_http"];
                if (body.contains("expose_https")) expose_https = body["expose_https"];
                if (body.contains("project")) project = body["project"];
            } catch (...) {
                res.status = 400; res.set_content(R"({"error": "Invalid JSON"})", "application/json"); return;
            }
        }
        try {
            MicroVM vm = manager.create_vm(vcpus, mem_mib, expose_ssh, expose_http, expose_https, project);
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
                std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_22.sock", vm.ip, 22, vm.id, wake_fn, status_fn).detach();
            }
            if (vm.http_exposed) {
                std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_80.sock", vm.ip, 80, vm.id, wake_fn, status_fn).detach();
            }
            if (vm.https_exposed) {
                std::thread(run_ghost_proxy, "/tmp/cryo_vm_" + std::to_string(vm.id) + "_443.sock", vm.ip, 443, vm.id, wake_fn, status_fn).detach();
            }

            json response = { {"id", vm.id}, {"ip", vm.ip}, {"status", "booting"} };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(/api/vms/(\d+)/restart)", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        manager.terminate_vm(id);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        try {
            MicroVM vm = manager.create_vm(1, 512, false, false, false, "default"); 
            std::thread([&manager, vm, KERNEL_PATH, ROOTFS_PATH]() { manager.configure_and_start(vm.id, KERNEL_PATH, ROOTFS_PATH); }).detach();
            res.set_content(R"({"status": "restarting"})", "application/json");
        } catch (const std::exception& e) { res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json"); }
    });

    svr.Delete(R"(/api/vms/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]); manager.terminate_vm(id);
        res.set_content(R"({"status": "deleted"})", "application/json");
    });

    svr.Post(R"(/api/vms/(\d+)/hibernate)", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        if (manager.hibernate_vm(id)) {
            res.set_content(R"({"status": "hibernated"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to hibernate"})", "application/json");
        }
    });

    svr.Post(R"(/api/vms/(\d+)/wake)", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        if (manager.wake_vm(id)) {
            res.set_content(R"({"status": "running"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to wake VM"})", "application/json");
        }
    });

    svr.Post(R"(/api/vms/(\d+)/proxy)", [&](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
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
        for (const auto& link : manager.get_all_links()) {
            response.push_back({{"vm1", link.vm1_id}, {"vm2", link.vm2_id}});
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/links", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            if (manager.link_microvms(body["vm1"], body["vm2"])) {
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

    svr.Delete(R"(/api/links/(\d+)/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        manager.unlink_microvms(std::stoi(req.matches[1]), std::stoi(req.matches[2]));
        res.set_content(json{{"status", "unlinked"}}.dump(), "application/json");
    });

    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

    std::cout << "[*] CryoSpawn Daemon running on http://localhost:8080\n";
    svr.set_mount_point("/", "./ui");
    svr.listen("0.0.0.0", 8080);
    return 0;
}
