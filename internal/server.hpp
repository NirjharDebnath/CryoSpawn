#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include "lib/httplib.h"
#include "lib/json.hpp"
#include "config.hpp"
#include "orchestrator.hpp"
#include "proxy.hpp"

using json = nlohmann::json;

struct ServerContext : public std::enable_shared_from_this<ServerContext> {
    VMManager& manager;
    std::string kernel_path;
    std::string rootfs_path;

    ServerContext(VMManager& mgr, std::string k_path, std::string r_path)
        : manager(mgr), kernel_path(std::move(k_path)), rootfs_path(std::move(r_path)) {}

    bool wake_fn(int vid) {
        return manager.start_or_resume_vm(vid, kernel_path, rootfs_path);
    }

    bool status_fn(int vid) {
        for (const auto& v : manager.list_vms(1, true)) {
            if (v.id == vid && (v.status == "hibernated" || v.status == "stopped")) return true;
        }
        return false;
    }

    void sync_vm_proxies(const MicroVM& vm) {
        std::string hex = manager.get_hex(vm.uuid);
        std::string uds_ssh = "/tmp/cryo-" + hex + "_22.sock";
        std::string uds_http = "/tmp/cryo-" + hex + "_80.sock";
        std::string uds_https = "/tmp/cryo-" + hex + "_443.sock";

        auto self = shared_from_this();
        WakeCallback w_fn = [self](int vid) { return self->wake_fn(vid); };
        StatusCallback s_fn = [self](int vid) { return self->status_fn(vid); };

        // SSH
        if (vm.ssh_exposed) {
            if (!is_ghost_proxy_running(uds_ssh)) {
                std::thread(run_ghost_proxy, uds_ssh, vm.ip, 22, vm.id, w_fn, s_fn, vm.id).detach();
            }
            if (vm.host_port > 0) {
                std::string tcp_key = "tcp:" + std::to_string(vm.host_port);
                if (!is_ghost_proxy_running(tcp_key)) {
                    std::thread(run_ghost_proxy_tcp, vm.host_port, vm.ip, 22, vm.id, w_fn, s_fn).detach();
                }
            }
        } else {
            if (is_ghost_proxy_running(uds_ssh)) stop_ghost_proxy(uds_ssh);
            if (vm.host_port > 0) stop_ghost_proxy("tcp:" + std::to_string(vm.host_port));
        }

        // HTTP
        if (vm.http_exposed) {
            if (!is_ghost_proxy_running(uds_http)) {
                std::thread(run_ghost_proxy, uds_http, vm.ip, 80, vm.id, w_fn, s_fn, vm.id).detach();
            }
        } else {
            if (is_ghost_proxy_running(uds_http)) stop_ghost_proxy(uds_http);
        }

        // HTTPS
        if (vm.https_exposed) {
            if (!is_ghost_proxy_running(uds_https)) {
                std::thread(run_ghost_proxy, uds_https, vm.ip, 443, vm.id, w_fn, s_fn, vm.id).detach();
            }
        } else {
            if (is_ghost_proxy_running(uds_https)) stop_ghost_proxy(uds_https);
        }
    }

    static bool authenticate(const httplib::Request& req, httplib::Response& res, int& user_id, bool& is_admin) {
        if (!req.has_header("X-User-ID")) {
            res.status = 401;
            res.set_content(R"({"error": "Unauthorized. Missing X-User-ID header."})", "application/json");
            return false;
        }
        try {
            user_id = std::stoi(req.get_header_value("X-User-ID"));
            if (user_id <= 0) {
                res.status = 400;
                res.set_content(R"({"error": "Invalid X-User-ID header."})", "application/json");
                return false;
            }
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid X-User-ID header."})", "application/json");
            return false;
        }
        is_admin = (req.has_header("X-Role") && req.get_header_value("X-Role") == "admin");
        return true;
    }
};

inline void register_routes(httplib::Server& svr, VMManager& manager, const std::string& KERNEL_PATH = "", const std::string& ROOTFS_PATH = "") {
    auto ctx = std::make_shared<ServerContext>(manager, KERNEL_PATH, ROOTFS_PATH);

    // Restore Ghost Proxies for all adopted VMs on startup
    for (const auto& vm : ctx->manager.list_vms(1, true)) {
        ctx->sync_vm_proxies(vm);
    }

    // Returns all active VMs accessible to user (strictly scoped to user unless all=true requested by admin)
    svr.Get("/api/vms", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        bool show_all = (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        std::string lan_ip = get_lan_ip(ctx->manager.active_iface);
        json response_json = json::array();
        for (const auto& vm : ctx->manager.list_vms(user_id, show_all)) {
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
                {"owner_id", vm.owner_id},
                {"allow_interproject_routing", vm.allow_interproject_routing}
            });
        }
        res.set_content(response_json.dump(), "application/json");
    });

    // Returns a single VM if authorized
    svr.Get(R"(^/api/vms/([a-zA-Z0-9-]+)$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        if (!is_admin && !ctx->manager.user_owns_vm(id, user_id)) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        MicroVM vm;
        if (!ctx->manager.get_vm(id, vm)) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }

        std::string lan_ip = get_lan_ip(ctx->manager.active_iface);
        json response_json = {
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
            {"owner_id", vm.owner_id},
            {"allow_interproject_routing", vm.allow_interproject_routing}
        };
        res.set_content(response_json.dump(), "application/json");
    });

    // Spawns a brand new microVM
    svr.Post("/api/vms", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        int vcpus = 1; int mem_mib = 512; 
        bool expose_ssh = false; bool expose_http = false; bool expose_https = false;
        int rootfs_gb = 3;
        bool allow_interproject_routing = false;
        std::string project = "default"; std::string name = "";
        int vm_owner = user_id;

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
                if (body.contains("allow_interproject_routing")) {
                    if (body["allow_interproject_routing"].is_boolean()) {
                        allow_interproject_routing = body["allow_interproject_routing"].get<bool>();
                    } else if (body["allow_interproject_routing"].is_number()) {
                        allow_interproject_routing = (body["allow_interproject_routing"].get<int>() != 0);
                    } else if (body["allow_interproject_routing"].is_string()) {
                        std::string s = body["allow_interproject_routing"].get<std::string>();
                        allow_interproject_routing = (s == "true" || s == "1");
                    }
                }
                if (is_admin && req.has_param("all") && body.contains("owner_id") && body["owner_id"].is_number_integer() && body["owner_id"].get<int>() > 0) {
                    vm_owner = body["owner_id"].get<int>();
                }
                if (rootfs_gb < 3) rootfs_gb = 3;
            } catch (...) {
                res.status = 400; res.set_content(R"({"error": "Invalid JSON"})", "application/json"); return;
            }
        }
        try {
            MicroVM vm = ctx->manager.create_vm(vcpus, mem_mib, expose_ssh, expose_http, expose_https, project, rootfs_gb, name, vm_owner, allow_interproject_routing);
            std::thread([ctx, vm]() {
                ctx->manager.configure_and_start(vm.id, ctx->kernel_path, ctx->rootfs_path);
            }).detach();

            ctx->sync_vm_proxies(vm);

            json response = {
                {"id", vm.id},
                {"uuid", vm.uuid},
                {"name", vm.name},
                {"ip", vm.ip},
                {"host_port", vm.host_port},
                {"status", "booting"},
                {"owner_id", vm.owner_id},
                {"project", vm.project_name},
                {"allow_interproject_routing", vm.allow_interproject_routing}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/restart$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;
        
        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden"})", "application/json"); return; }

        MicroVM vm;
        if (ctx->manager.get_vm(id, vm) && vm.status == "terminating") {
            res.status = 400; res.set_content(R"({"error": "Cannot restart a terminating VM"})", "application/json"); return;
        }

        ctx->manager.reboot_vm(id, ctx->kernel_path, ctx->rootfs_path);
        res.set_content(R"({"status": "restarting"})", "application/json");
    });

    // Dynamically opens or closes SSH/HTTP/HTTPS ports and allows routing toggle
    svr.Patch(R"(^/api/vms/([a-zA-Z0-9-]+)/ports$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;
        
        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400; res.set_content(R"({"error": "Payload must be a JSON object"})", "application/json"); return;
            }
            MicroVM current_vm;
            if (!ctx->manager.get_vm(id, current_vm)) {
                res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return;
            }
            if (current_vm.status == "terminating") {
                res.status = 400; res.set_content(R"({"error": "Cannot update ports for a terminating VM"})", "application/json"); return;
            }

            bool ssh = body.value("ssh_exposed", current_vm.ssh_exposed);
            bool http = body.value("http_exposed", current_vm.http_exposed);
            bool https = body.value("https_exposed", current_vm.https_exposed);

            ctx->manager.update_vm_ports(id, ssh, http, https);

            if (body.contains("allow_interproject_routing")) {
                bool allow = false;
                if (body["allow_interproject_routing"].is_boolean()) {
                    allow = body["allow_interproject_routing"].get<bool>();
                } else if (body["allow_interproject_routing"].is_number()) {
                    allow = (body["allow_interproject_routing"].get<int>() != 0);
                } else if (body["allow_interproject_routing"].is_string()) {
                    std::string s = body["allow_interproject_routing"].get<std::string>();
                    allow = (s == "true" || s == "1");
                }
                ctx->manager.update_vm_routing(id, allow);
            }

            MicroVM target_vm;
            if (ctx->manager.get_vm(id, target_vm)) {
                ctx->sync_vm_proxies(target_vm);
            }
            res.set_content(R"({"status": "ports updated"})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // Dedicated endpoint for inter-project routing permissions
    auto handle_update_routing = [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400; res.set_content(R"({"error": "Payload must be a JSON object"})", "application/json"); return;
            }
            MicroVM current_vm;
            if (!ctx->manager.get_vm(id, current_vm)) {
                res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return;
            }
            if (current_vm.status == "terminating") {
                res.status = 400; res.set_content(R"({"error": "Cannot update routing for a terminating VM"})", "application/json"); return;
            }
            if (!body.contains("allow_interproject_routing")) {
                res.status = 400;
                res.set_content(R"({"error": "Missing allow_interproject_routing field."})", "application/json");
                return;
            }
            bool allow = false;
            if (body["allow_interproject_routing"].is_boolean()) {
                allow = body["allow_interproject_routing"].get<bool>();
            } else if (body["allow_interproject_routing"].is_number()) {
                allow = (body["allow_interproject_routing"].get<int>() != 0);
            } else if (body["allow_interproject_routing"].is_string()) {
                std::string s = body["allow_interproject_routing"].get<std::string>();
                allow = (s == "true" || s == "1");
            }
            ctx->manager.update_vm_routing(id, allow);
            res.set_content(json{{"status", "routing updated"}, {"allow_interproject_routing", allow}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    };
    svr.Patch(R"(^/api/vms/([a-zA-Z0-9-]+)/routing$)", handle_update_routing);
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/routing$)", handle_update_routing);

    // Nukes a VM from existence
    svr.Delete(R"(^/api/vms/([a-zA-Z0-9-]+)$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; } 
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        ctx->manager.terminate_vm(id);
        res.set_content(R"({"status": "deleted"})", "application/json");
    });

    // Freezes VM to disk (dumps RAM & CPU snapshot)
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/hibernate$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;
        
        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        MicroVM vm;
        if (ctx->manager.get_vm(id, vm) && vm.status == "terminating") {
            res.status = 400; res.set_content(R"({"error": "Cannot hibernate a terminating VM"})", "application/json"); return;
        }

        if (ctx->manager.hibernate_vm(id)) {
            res.set_content(R"({"status": "hibernated"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to hibernate"})", "application/json");
        }
    });

    // Thaws or boots a microVM (wake & start unified)
    auto handle_wake_or_start = [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;
        
        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        MicroVM vm;
        if (ctx->manager.get_vm(id, vm) && vm.status == "terminating") {
            res.status = 400; res.set_content(R"({"error": "Cannot start or wake a terminating VM"})", "application/json"); return;
        }

        if (ctx->manager.start_or_resume_vm(id, ctx->kernel_path, ctx->rootfs_path)) {
            res.set_content(R"({"status": "running"})", "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "Failed to start or wake VM"})", "application/json");
        }
    };

    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/wake$)", handle_wake_or_start);
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/start$)", handle_wake_or_start);

    // Dynamic ghost proxy creation
    svr.Post(R"(^/api/vms/([a-zA-Z0-9-]+)/proxy$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;
        
        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int id = ctx->manager.get_slot_by_uuid(req.matches[1]);
        if (id == -1) { res.status = 404; res.set_content(R"({"error": "VM not found"})", "application/json"); return; }
        bool can_modify = ctx->manager.user_owns_vm(id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) { res.status = 403; res.set_content(R"({"error": "Forbidden: You do not have permission to access or modify this VM."})", "application/json"); return; }

        MicroVM current_vm;
        if (ctx->manager.get_vm(id, current_vm) && current_vm.status == "terminating") {
            res.status = 400; res.set_content(R"({"error": "Cannot create proxy for a terminating VM"})", "application/json"); return;
        }

        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400; res.set_content(R"({"error": "Payload must be a JSON object"})", "application/json"); return;
            }
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
            for (const auto& v : ctx->manager.list_vms(1, true)) {
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

            std::string uds_path = "/tmp/cryo-" + ctx->manager.get_hex(vm_uuid) + "_" + std::to_string(vm_port) + ".sock";
            WakeCallback w_fn = [ctx](int vid) { return ctx->wake_fn(vid); };
            StatusCallback s_fn = [ctx](int vid) { return ctx->status_fn(vid); };

            if (!is_ghost_proxy_running(uds_path)) {
                std::thread(run_ghost_proxy, uds_path, vm_ip, vm_port, id, w_fn, s_fn, id).detach();
            }

            if (host_port > 0) {
                std::string tcp_key = "tcp:" + std::to_string(host_port);
                if (!is_ghost_proxy_running(tcp_key)) {
                    std::thread(run_ghost_proxy_tcp, host_port, vm_ip, vm_port, id, w_fn, s_fn).detach();
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
    svr.Get("/api/links", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        bool show_all = (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        json response = json::array();
        auto vms = ctx->manager.list_vms(user_id, show_all);
        auto get_uuid = [&](int id) {
            for (const auto& v : vms) if (v.id == id) return v.uuid;
            return std::string("");
        };
        for (const auto& link : ctx->manager.get_all_links(user_id, show_all)) {
            std::string u1 = get_uuid(link.vm1_id);
            std::string u2 = get_uuid(link.vm2_id);
            if (!u1.empty() && !u2.empty()) {
                response.push_back({{"vm1_uuid", u1}, {"vm2_uuid", u2}});
            }
        }
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/links", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400; res.set_content(R"({"error": "Payload must be a JSON object"})", "application/json"); return;
            }
            std::string vm1_uuid = body.value("vm1_id", "");
            std::string vm2_uuid = body.value("vm2_id", "");
            if (vm1_uuid.empty() || vm2_uuid.empty() || vm1_uuid == vm2_uuid) {
                res.status = 400;
                res.set_content(json{{"error", "Please provide two different, valid VM UUIDs."}}.dump(), "application/json");
                return;
            }
            int id1 = ctx->manager.get_slot_by_uuid(vm1_uuid);
            int id2 = ctx->manager.get_slot_by_uuid(vm2_uuid);
            if (id1 == -1 || id2 == -1) {
                res.status = 404;
                res.set_content(json{{"error", "One or both VMs not found."}}.dump(), "application/json");
                return;
            }
            bool allow_override = (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
            if (!ctx->manager.can_user_link(id1, id2, user_id, allow_override)) {
                res.status = 403;
                res.set_content(json{{"error", "Permission denied. Cross-user bridging is forbidden and you can only link your own VMs."}}.dump(), "application/json");
                return;
            }
            if (!ctx->manager.can_route_interproject(id1, id2)) {
                res.status = 400;
                res.set_content(json{{"error", "Cross-project linking is denied. Both VMs must opt into inter-project routing."}}.dump(), "application/json");
                return;
            }
            int link_owner = ctx->manager.get_vm_owner(id1);
            if (link_owner <= 0) link_owner = user_id;
            if (ctx->manager.link_microvms(id1, id2, link_owner)) {
                res.set_content(json{{"status", "linked"}}.dump(), "application/json");
            } else {
                res.status = 400; 
                res.set_content(json{{"error", "Failed to link MicroVMs."}}.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    auto handle_delete_link = [ctx](const httplib::Request& req, httplib::Response& res, const std::string& vm1_uuid, const std::string& vm2_uuid) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (vm1_uuid.empty() || vm2_uuid.empty() || vm1_uuid == vm2_uuid) {
            res.status = 400;
            res.set_content(json{{"error", "Please provide two different, valid VM UUIDs."}}.dump(), "application/json");
            return;
        }

        int id1 = ctx->manager.get_slot_by_uuid(vm1_uuid);
        int id2 = ctx->manager.get_slot_by_uuid(vm2_uuid);

        if (id1 == -1 && id2 == -1) {
            res.status = 404;
            res.set_content(json{{"error", "One or both VMs not found."}}.dump(), "application/json");
            return;
        }

        bool allow_override = (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (id1 == -1 || id2 == -1) {
            int surviving_id = (id1 != -1) ? id1 : id2;
            if (!allow_override && !ctx->manager.user_owns_vm(surviving_id, user_id)) {
                res.status = 403;
                res.set_content(json{{"error", "Permission denied."}}.dump(), "application/json");
                return;
            }
            ctx->manager.db.cleanup_orphaned_links();
            res.set_content(json{{"status", "unlinked"}}.dump(), "application/json");
            return;
        }

        if (!ctx->manager.can_user_unlink(id1, id2, user_id, allow_override)) {
            res.status = 403;
            res.set_content(json{{"error", "Permission denied."}}.dump(), "application/json");
            return;
        }
        ctx->manager.unlink_microvms(id1, id2);
        res.set_content(json{{"status", "unlinked"}}.dump(), "application/json");
    };

    svr.Delete(R"(^/api/links/([a-zA-Z0-9-]+)/([a-zA-Z0-9-]+)$)", [handle_delete_link](const httplib::Request& req, httplib::Response& res) {
        if (req.matches.size() < 3) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        handle_delete_link(req, res, req.matches[1], req.matches[2]);
    });

    svr.Delete("/api/links", [handle_delete_link](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400;
                res.set_content(json{{"error", "Payload must be a JSON object."}}.dump(), "application/json");
                return;
            }
            std::string vm1_uuid = body.value("vm1_id", "");
            std::string vm2_uuid = body.value("vm2_id", "");
            handle_delete_link(req, res, vm1_uuid, vm2_uuid);
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // --- VOLUMES API ---
    svr.Get("/api/volumes", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        bool show_all = (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        auto vms = ctx->manager.list_vms(user_id, show_all);
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
        for (const auto& vol : ctx->manager.db.get_volumes(user_id, show_all)) {
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

    svr.Get(R"(^/api/volumes/(\d+)$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        try {
            int vol_id = std::stoi(req.matches[1]);
            Database::VolumeRow vol;
            if (!ctx->manager.db.get_volume(vol_id, vol)) {
                res.status = 404;
                res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
                return;
            }
            if (!is_admin && vol.owner_id != user_id) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
                return;
            }

            std::string vm_uuid = "", vm_name = "";
            if (vol.attached_vm_id != -1) {
                for (const auto& v : ctx->manager.list_vms(1, true)) {
                    if (v.id == vol.attached_vm_id) {
                        vm_uuid = v.uuid;
                        vm_name = v.name;
                        break;
                    }
                }
            }

            json response = {
                {"id", vol.id},
                {"name", vol.name},
                {"size_gb", vol.size_gb},
                {"attached_vm_id", vol.attached_vm_id},
                {"attached_vm_uuid", vm_uuid},
                {"attached_vm_name", vm_name},
                {"owner_id", vol.owner_id}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post("/api/volumes", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400;
                res.set_content(json{{"error", "Payload must be a JSON object."}}.dump(), "application/json");
                return;
            }
            std::string name = body.value("name", "");
            int size_gb = body.value("size_gb", 1);
            if (name.empty() || size_gb < 1 || size_gb > 1000) {
                res.status = 400;
                res.set_content(json{{"error", "Invalid volume name or size (1-1000 GB)."}}.dump(), "application/json");
                return;
            }
            int vol_owner = user_id;
            if (is_admin && req.has_param("all") && body.contains("owner_id") && body["owner_id"].is_number_integer() && body["owner_id"].get<int>() > 0) {
                vol_owner = body["owner_id"].get<int>();
            }
            int id = ctx->manager.create_volume(name, size_gb, vol_owner);
            res.set_content(json{{"status", "created"}, {"id", id}}.dump(), "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400; res.set_content(json{{"error", "Invalid JSON payload"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Delete(R"(^/api/volumes/(\d+)$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        try {
            int vol_id = std::stoi(req.matches[1]);
            Database::VolumeRow vol;
            if (!ctx->manager.db.get_volume(vol_id, vol)) {
                res.status = 404;
                res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
                return;
            }
            bool can_modify = (vol.owner_id == user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
            if (!can_modify) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
                return;
            }
            if (vol.attached_vm_id != -1) {
                res.status = 400;
                res.set_content(json{{"error", "Cannot delete an attached volume. Please detach it first."}}.dump(), "application/json");
                return;
            }
            ctx->manager.delete_volume(vol_id);
            res.set_content(json{{"status", "deleted"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/volumes/(\d+)/attach$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int vol_id = -1;
        try {
            vol_id = std::stoi(req.matches[1]);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid volume ID."}}.dump(), "application/json");
            return;
        }

        Database::VolumeRow vol;
        if (!ctx->manager.db.get_volume(vol_id, vol)) {
            res.status = 404;
            res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
            return;
        }
        bool can_modify = (vol.owner_id == user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify) {
            res.status = 403;
            res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
            return;
        }
        if (vol.attached_vm_id != -1) {
            res.status = 400;
            res.set_content(json{{"error", "Volume is already attached to a VM. Please detach it first."}}.dump(), "application/json");
            return;
        }

        std::string vm_uuid = "";
        try {
            auto body = json::parse(req.body);
            if (!body.is_object()) {
                res.status = 400;
                res.set_content(json{{"error", "Payload must be a JSON object."}}.dump(), "application/json");
                return;
            }
            vm_uuid = body.value("vm_id", "");
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON payload."}}.dump(), "application/json");
            return;
        }

        int vm_id = ctx->manager.get_slot_by_uuid(vm_uuid);
        if (vm_id == -1) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid or missing VM UUID."}}.dump(), "application/json");
            return;
        }
        bool can_modify_vm = ctx->manager.user_owns_vm(vm_id, user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
        if (!can_modify_vm) {
            res.status = 403;
            res.set_content(json{{"error", "Forbidden: You do not own the target VM."}}.dump(), "application/json");
            return;
        }

        MicroVM target_vm;
        if (ctx->manager.get_vm(vm_id, target_vm) && target_vm.status == "terminating") {
            res.status = 400;
            res.set_content(json{{"error", "Cannot attach volume to a terminating VM."}}.dump(), "application/json");
            return;
        }

        int target_owner = ctx->manager.get_vm_owner(vm_id);
        if (vol.owner_id != target_owner) {
            res.status = 403;
            res.set_content(json{{"error", "Forbidden: Cross-tenant volume attachment is forbidden."}}.dump(), "application/json");
            return;
        }

        try {
            ctx->manager.db.attach_volume(vol_id, vm_id);
            
            // Check if VM is running or hibernated
            for (const auto& v : ctx->manager.list_vms(1, true)) {
                if (v.id == vm_id) {
                    if (v.status == "running") {
                        ctx->manager.reboot_vm(vm_id, ctx->kernel_path, ctx->rootfs_path);
                    } else if (v.status == "hibernated") {
                        // Invalidate snapshot so the new volume is properly configured on next cold boot
                        std::string snap = config::BASE_PATH + "/instances/cryo-" + ctx->manager.get_hex(v.uuid) + "_state.snap";
                        std::string mem = config::BASE_PATH + "/instances/cryo-" + ctx->manager.get_hex(v.uuid) + "_mem.ram";
                        std::error_code ec;
                        std::filesystem::remove(snap, ec);
                        std::filesystem::remove(mem, ec);
                        ctx->manager.set_vm_status(vm_id, "stopped");
                    }
                    break;
                }
            }
            res.set_content(json{{"status", "attached"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr.Post(R"(^/api/volumes/(\d+)/detach$)", [ctx](const httplib::Request& req, httplib::Response& res) {
        int user_id; bool is_admin;
        if (!ServerContext::authenticate(req, res, user_id, is_admin)) return;

        if (req.matches.size() < 2) { res.status = 400; res.set_content(R"({"error": "Bad request"})", "application/json"); return; }
        int vol_id = -1;
        try {
            vol_id = std::stoi(req.matches[1]);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid volume ID."}}.dump(), "application/json");
            return;
        }

        try {
            Database::VolumeRow vol;
            if (!ctx->manager.db.get_volume(vol_id, vol)) {
                res.status = 404;
                res.set_content(json{{"error", "Volume not found"}}.dump(), "application/json");
                return;
            }
            bool can_modify = (vol.owner_id == user_id) || (is_admin && req.has_param("all") && req.get_param_value("all") == "true");
            if (!can_modify) {
                res.status = 403;
                res.set_content(json{{"error", "Forbidden: You do not own this volume."}}.dump(), "application/json");
                return;
            }

            int attached_vm = vol.attached_vm_id;
            ctx->manager.db.detach_volume(vol_id);
            
            if (attached_vm != -1) {
                for (const auto& v : ctx->manager.list_vms(1, true)) {
                    if (v.id == attached_vm) {
                        if (v.status == "running") {
                            ctx->manager.reboot_vm(attached_vm, ctx->kernel_path, ctx->rootfs_path);
                        } else if (v.status == "hibernated") {
                            std::string snap = config::BASE_PATH + "/instances/cryo-" + ctx->manager.get_hex(v.uuid) + "_state.snap";
                            std::string mem = config::BASE_PATH + "/instances/cryo-" + ctx->manager.get_hex(v.uuid) + "_mem.ram";
                            std::error_code ec;
                            std::filesystem::remove(snap, ec);
                            std::filesystem::remove(mem, ec);
                            ctx->manager.set_vm_status(attached_vm, "stopped");
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
}
