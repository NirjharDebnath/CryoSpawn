#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include "../internal/config.hpp"
#include "../internal/database.hpp"
#include "../internal/proxy.hpp"
#include "../internal/server.hpp"

void test_database_and_slots() {
    std::cout << "[TEST] Testing Database operations & slot allocation...\n";
    std::string test_db = "/tmp/test_cryospawn_" + std::to_string(getpid()) + ".db";
    unlink(test_db.c_str());

    {
        Database db(test_db);
        
        // 1. Initial slot on empty table should be 0
        int slot = db.get_next_free_slot();
        assert(slot == 0);
        std::cout << "  [✓] Empty table yields slot 0\n";

        // 2. Insert slot 0
        db.insert_vm(0, 1001, "stopped", 1, 512, "172.16.0.2", "172.16.0.1", "cryo-test0", false, 0, "default", false, false, 3, "vm-00000000", "test-0", 1);
        
        // 3. Next free slot should be 1
        slot = db.get_next_free_slot();
        assert(slot == 1);
        std::cout << "  [✓] After slot 0, next slot is 1\n";

        // 4. Insert slot 1
        db.insert_vm(1, 1002, "stopped", 1, 512, "172.16.0.6", "172.16.0.5", "cryo-test1", false, 0, "default", false, false, 3, "vm-00000001", "test-1", 1);
        slot = db.get_next_free_slot();
        assert(slot == 2);
        std::cout << "  [✓] After slots 0 & 1, next slot is 2\n";

        // 5. Delete slot 0: next free slot MUST be 0 (reclaiming gap!)
        db.remove_vm(0);
        slot = db.get_next_free_slot();
        assert(slot == 0);
        std::cout << "  [✓] After deleting slot 0, slot 0 is correctly reclaimed!\n";

        // 6. Test multi-tenancy in get_all_vms
        // User 2 inserts a VM in slot 0
        db.insert_vm(0, 2001, "stopped", 1, 512, "172.16.0.2", "172.16.0.1", "cryo-test0", false, 0, "user2-proj", false, false, 3, "vm-user2vm", "u2-vm", 2, false);
        
        // User 2 should see only 1 VM (slot 0)
        auto u2_vms = db.get_all_vms(2, false);
        assert(u2_vms.size() == 1);
        assert(u2_vms[0].slot == 0);
        assert(u2_vms[0].owner_id == 2);
        assert(u2_vms[0].allow_interproject_routing == false);

        // Update routing permission
        db.update_vm_routing(0, true);
        u2_vms = db.get_all_vms(2, false);
        assert(u2_vms[0].allow_interproject_routing == true);
        std::cout << "  [✓] MicroVM allow_interproject_routing persistence verified\n";

        // User 1 should see only 1 VM (slot 1)
        auto u1_vms = db.get_all_vms(1, false);
        assert(u1_vms.size() == 1);
        assert(u1_vms[0].slot == 1);
        assert(u1_vms[0].owner_id == 1);

        // Admin should see both VMs (slots 0 and 1)
        auto admin_vms = db.get_all_vms(1, true);
        assert(admin_vms.size() == 2);
        std::cout << "  [✓] Multi-tenant VM filtering verified (Admin sees all, Users see own)\n";

        // 7. Test Volumes
        int vol1 = db.insert_volume("vol_u1", 5, 1);
        int vol2 = db.insert_volume("vol_u2", 10, 2);
        assert(vol1 > 0 && vol2 > 0);

        auto u1_vols = db.get_volumes(1, false);
        assert(u1_vols.size() == 1 && u1_vols[0].name == "vol_u1");

        auto u2_vols = db.get_volumes(2, false);
        assert(u2_vols.size() == 1 && u2_vols[0].name == "vol_u2");

        auto admin_vols = db.get_volumes(1, true);
        assert(admin_vols.size() == 2);
        std::cout << "  [✓] Multi-tenant volume filtering verified\n";

        // 8. Test Volume Attachment & Detachment
        Database::VolumeRow vrow;
        assert(db.get_volume(vol1, vrow));
        assert(vrow.attached_vm_id == -1);

        db.attach_volume(vol1, 1);
        assert(db.get_volume(vol1, vrow));
        assert(vrow.attached_vm_id == 1);

        db.detach_volume(vol1);
        assert(db.get_volume(vol1, vrow));
        assert(vrow.attached_vm_id == -1);
        std::cout << "  [✓] Volume attachment and detachment verified\n";

        // 9. Test Network Links
        bool inserted1 = db.add_link("user2-proj", 0, 1, 2);
        assert(inserted1 == true);
        // Duplicate link should return false (ignore)
        bool inserted2 = db.add_link("user2-proj", 0, 1, 2);
        assert(inserted2 == false);

        auto links = db.get_all_links(2, false);
        assert(links.size() == 1);

        // User 1 should NOT see user 2's link
        auto u1_links = db.get_all_links(1, false);
        assert(u1_links.empty());

        // Admin should see user 2's link
        auto admin_links = db.get_all_links(1, true);
        assert(admin_links.size() == 1);
        std::cout << "  [✓] Multi-tenant network link scoping verified\n";

        db.remove_link(0, 1);
        links = db.get_all_links(2, false);
        assert(links.empty());
        std::cout << "  [✓] Network links insertion, deduplication, and removal verified\n";
    }

    unlink(test_db.c_str());
    unlink((test_db + "-wal").c_str());
    unlink((test_db + "-shm").c_str());
}

void test_ghost_proxy_tcp() {
    std::cout << "[TEST] Testing TCP Ghost Proxy...\n";
    signal(SIGPIPE, SIG_IGN);

    int test_port = 19876;
    int mock_vm_port = 19877;

    // Start a mock guest service on 127.0.0.1:19877
    int guest_srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(guest_srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in gaddr{};
    gaddr.sin_family = AF_INET;
    gaddr.sin_port = htons(mock_vm_port);
    gaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(guest_srv, (struct sockaddr*)&gaddr, sizeof(gaddr)) == 0);
    assert(listen(guest_srv, 5) == 0);

    std::thread guest_thread([guest_srv]() {
        int client = accept(guest_srv, nullptr, nullptr);
        if (client >= 0) {
            char buf[64];
            ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                std::string reply = "ECHO:" + std::string(buf);
                send(client, reply.c_str(), reply.length(), MSG_NOSIGNAL);
            }
            close(client);
        }
        close(guest_srv);
    });

    bool wake_called = false;
    auto wake_fn = [&wake_called](int) { wake_called = true; return true; };
    auto status_fn = [](int) { return false; }; // already awake

    std::thread proxy_thread([test_port, mock_vm_port, wake_fn, status_fn]() {
        run_ghost_proxy_tcp(test_port, "127.0.0.1", mock_vm_port, 999, wake_fn, status_fn);
    });
    proxy_thread.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(is_ghost_proxy_running("tcp:" + std::to_string(test_port)));

    // Connect to Ghost Proxy TCP port
    int client = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in paddr{};
    paddr.sin_family = AF_INET;
    paddr.sin_port = htons(test_port);
    paddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(client, (struct sockaddr*)&paddr, sizeof(paddr)) == 0);

    const char* msg = "HelloCryo";
    send(client, msg, strlen(msg), MSG_NOSIGNAL);

    char reply_buf[128];
    ssize_t rn = recv(client, reply_buf, sizeof(reply_buf) - 1, 0);
    assert(rn > 0);
    reply_buf[rn] = '\0';
    assert(std::string(reply_buf) == "ECHO:HelloCryo");
    close(client);

    guest_thread.join();
    stop_ghost_proxy("tcp:" + std::to_string(test_port));
    assert(!is_ghost_proxy_running("tcp:" + std::to_string(test_port)));
    std::cout << "  [✓] TCP Ghost Proxy successfully forwarded traffic & stopped cleanly!\n";
}

void test_multitenant_and_interproject_routing_logic() {
    std::cout << "[TEST] Testing Multi-Tenant Isolation & Inter-Project Routing Logic...\n";
    std::string test_db = "/tmp/test_cryospawn_logic_" + std::to_string(getpid()) + ".db";
    unlink(test_db.c_str());

    {
        VMManager manager(test_db);

        MicroVM v1;
        v1.id = 0; v1.uuid = "vm-u1-p1"; v1.owner_id = 10; v1.project_name = "project-A";
        v1.status = "stopped"; v1.allow_interproject_routing = false; v1.tap_name = "cryo-u1p1";
        manager.add_vm_for_test(v1);

        MicroVM v2;
        v2.id = 1; v2.uuid = "vm-u1-p2"; v2.owner_id = 10; v2.project_name = "project-B";
        v2.status = "stopped"; v2.allow_interproject_routing = false; v2.tap_name = "cryo-u1p2";
        manager.add_vm_for_test(v2);

        MicroVM v3;
        v3.id = 2; v3.uuid = "vm-u1-p1-b"; v3.owner_id = 10; v3.project_name = "project-A";
        v3.status = "stopped"; v3.allow_interproject_routing = false; v3.tap_name = "cryo-u1p1b";
        manager.add_vm_for_test(v3);

        MicroVM v4;
        v4.id = 3; v4.uuid = "vm-u2-p1"; v4.owner_id = 20; v4.project_name = "project-A";
        v4.status = "stopped"; v4.allow_interproject_routing = true; v4.tap_name = "cryo-u2p1";
        manager.add_vm_for_test(v4);

        // 1. Cross-tenant permission check
        // User 10 linking v1 and v4 (v4 belongs to user 20) -> MUST FAIL
        assert(!manager.can_user_link(v1.id, v4.id, 10, false));
        // Admin linking v1 and v4 -> MUST FAIL (cross-user links strictly forbidden)
        assert(!manager.can_user_link(v1.id, v4.id, 1, true));
        // User 20 linking v1 and v2 (both owned by 10) -> MUST FAIL
        assert(!manager.can_user_link(v1.id, v2.id, 20, false));
        // User 10 linking v1 and v2 (both owned by 10) -> ALLOWED
        assert(manager.can_user_link(v1.id, v2.id, 10, false));
        // Admin linking v1 and v2 (both owned by 10) -> ALLOWED
        assert(manager.can_user_link(v1.id, v2.id, 1, true));
        std::cout << "  [✓] can_user_link enforces strict multi-tenant boundary\n";

        // 2. Same-project linking
        // v1 and v3 are both project-A, owned by 10 -> allowed regardless of allow_interproject_routing
        assert(manager.can_route_interproject(v1.id, v3.id));
        assert(manager.link_microvms(v1.id, v3.id, 10));
        std::cout << "  [✓] Same project linking succeeds\n";

        // 3. Cross-project linking with default allow_interproject_routing = false
        // v1 (project-A, false) and v2 (project-B, false)
        assert(!manager.can_route_interproject(v1.id, v2.id));
        assert(!manager.link_microvms(v1.id, v2.id, 10));
        std::cout << "  [✓] Cross-project linking rejected when allow_interproject_routing is false\n";

        // 4. Opt-in: one VM true, one false
        manager.update_vm_routing(v1.id, true);
        assert(!manager.can_route_interproject(v1.id, v2.id));
        assert(!manager.link_microvms(v1.id, v2.id, 10));
        std::cout << "  [✓] Cross-project linking rejected when only one VM is opted in\n";

        // 5. Opt-in: both VMs true
        manager.update_vm_routing(v2.id, true);
        assert(manager.can_route_interproject(v1.id, v2.id));
        assert(manager.link_microvms(v1.id, v2.id, 10));
        std::cout << "  [✓] Cross-project linking succeeds when both VMs opt into inter-project routing\n";

        // 6. Cross-user VMs even if both have allow_interproject_routing = true
        manager.update_vm_routing(v4.id, true);
        assert(!manager.link_microvms(v1.id, v4.id, 10));
        std::cout << "  [✓] Cross-tenant linking forbidden even if both VMs opt into routing\n";

        // 7. Self-linking rejection
        assert(!manager.can_user_link(v1.id, v1.id, 10, false));
        assert(!manager.can_route_interproject(v1.id, v1.id));
        assert(!manager.link_microvms(v1.id, v1.id, 10));
        std::cout << "  [✓] Self-linking is strictly forbidden\n";

        // 8. Proactive revocation teardown:
        // v1 <-> v3 is same project (project-A)
        // v1 <-> v2 is cross-project (project-A <-> project-B)
        // Toggling allow_interproject_routing to false on v1 must prune the cross-project link while keeping same-project intact
        manager.update_vm_routing(v1.id, false);
        auto active_links = manager.get_all_links(10, false);
        assert(active_links.size() == 1);
        assert((active_links[0].vm1_id == std::min(v1.id, v3.id)) && (active_links[0].vm2_id == std::max(v1.id, v3.id)));
        std::cout << "  [✓] Revoking allow_interproject_routing proactively severs cross-project links in SQLite & TAPs\n";

        // 9. VM termination cleanly tears down all associated links (same-project and cross-project)
        // Re-enable routing and re-link v1 <-> v2
        manager.update_vm_routing(v1.id, true);
        assert(manager.link_microvms(v1.id, v2.id, 10));
        assert(manager.get_all_links(10, false).size() == 2);

        // Terminating v1 must cleanly remove all links connected to v1
        manager.terminate_vm(v1.id);
        assert(manager.get_all_links(10, false).empty());
        std::cout << "  [✓] Terminating VM cleanly sweeps all connected same- and cross-project links\n";

        // 10. Unlinking after one of the participating VMs was destroyed
        // Calling unlink_microvms for v1 and v2 should succeed gracefully and ensure no DB entry remains
        assert(manager.unlink_microvms(v1.id, v2.id));
        std::cout << "  [✓] Unlinking after one participating VM is destroyed completes gracefully\n";

        // 11. Terminating VMs cannot be linked
        MicroVM v_term;
        v_term.id = 88; v_term.uuid = "vm-terminating"; v_term.owner_id = 10; v_term.project_name = "project-A";
        v_term.status = "terminating"; v_term.allow_interproject_routing = true; v_term.tap_name = "cryo-term";
        manager.add_vm_for_test(v_term);

        assert(!manager.can_user_link(v2.id, v_term.id, 10, false));
        assert(!manager.can_route_interproject(v2.id, v_term.id));
        assert(!manager.link_microvms(v2.id, v_term.id, 10));
        std::cout << "  [✓] Terminating VMs cannot participate in new network links\n";

        // 12. Orphaned link cleanup
        // Insert an orphaned link with non-existent slot IDs directly in DB
        Database local_db(test_db);
        local_db.add_link("orphaned-proj", 991, 992, 10);
        assert(!local_db.get_all_links(10, false).empty());
        local_db.cleanup_orphaned_links();
        assert(local_db.get_all_links(10, false).empty());
        std::cout << "  [✓] Orphaned link cleanup successfully removes dangling links\n";
    }

    unlink(test_db.c_str());
    unlink((test_db + "-wal").c_str());
    unlink((test_db + "-shm").c_str());
}

void test_api_server_multitenancy_and_interproject() {
    std::cout << "[TEST] Testing REST API Multi-Tenancy & Inter-Project Endpoints...\n";
    std::string test_db = "/tmp/test_cryospawn_api_" + std::to_string(getpid()) + ".db";
    unlink(test_db.c_str());

    int api_port = 29990 + (getpid() % 1000);
    httplib::Server svr;
    VMManager manager(test_db);

    register_routes(svr, manager, "", "");

    MicroVM v1;
    v1.id = 0; v1.uuid = "vm-u10-alpha"; v1.name = "v1"; v1.owner_id = 10; v1.project_name = "proj-alpha";
    v1.status = "stopped"; v1.allow_interproject_routing = false; v1.tap_name = "cryo-u10a";
    manager.add_vm_for_test(v1);

    MicroVM v2;
    v2.id = 1; v2.uuid = "vm-u10-beta"; v2.name = "v2"; v2.owner_id = 10; v2.project_name = "proj-beta";
    v2.status = "stopped"; v2.allow_interproject_routing = false; v2.tap_name = "cryo-u10b";
    manager.add_vm_for_test(v2);

    MicroVM v3;
    v3.id = 2; v3.uuid = "vm-u10-alpha2"; v3.name = "v3"; v3.owner_id = 10; v3.project_name = "proj-alpha";
    v3.status = "stopped"; v3.allow_interproject_routing = false; v3.tap_name = "cryo-u10a2";
    manager.add_vm_for_test(v3);

    MicroVM v4;
    v4.id = 3; v4.uuid = "vm-u20-alpha"; v4.name = "v4"; v4.owner_id = 20; v4.project_name = "proj-alpha";
    v4.status = "stopped"; v4.allow_interproject_routing = true; v4.tap_name = "cryo-u20a";
    manager.add_vm_for_test(v4);

    int vol_u10 = manager.create_volume("vol_u10", 10, 10);
    int vol_u20 = manager.create_volume("vol_u20", 20, 20);

    std::thread server_thread([&svr, api_port]() {
        svr.listen("127.0.0.1", api_port);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client cli("127.0.0.1", api_port);
    cli.set_connection_timeout(5);

    // 1. Direct API requests GET /api/vms return only tenant's resources
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        auto res = cli.Get("/api/vms", u10_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.is_array());
        assert(j.size() == 3);
        for (const auto& item : j) {
            assert(item["owner_id"] == 10);
        }
    }
    {
        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};
        auto res = cli.Get("/api/vms", u20_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.is_array());
        assert(j.size() == 1);
        assert(j[0]["owner_id"] == 20);
        assert(j[0]["uuid"] == "vm-u20-alpha");
    }
    {
        httplib::Headers admin_hdrs = {{"X-User-ID", "1"}, {"X-Role", "admin"}};
        auto res = cli.Get("/api/vms", admin_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.size() == 4);
        std::cout << "  [✓] GET /api/vms strictly scoped by owner_id (admin sees all)\n";
    }

    // 2. Direct API requests GET /api/volumes return only tenant's resources
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        auto res = cli.Get("/api/volumes", u10_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.size() == 1);
        assert(j[0]["owner_id"] == 10);
    }
    {
        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};
        auto res = cli.Get("/api/volumes", u20_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.size() == 1);
        assert(j[0]["owner_id"] == 20);
    }
    {
        httplib::Headers admin_hdrs = {{"X-User-ID", "1"}, {"X-Role", "admin"}};
        auto res = cli.Get("/api/volumes", admin_hdrs);
        assert(res && res->status == 200);
        auto j = json::parse(res->body);
        assert(j.size() == 2);
        std::cout << "  [✓] GET /api/volumes strictly scoped by owner_id\n";
    }

    // 3. Attempting to restart, hibernate, delete another tenant's VM returns 403 Forbidden
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        auto res1 = cli.Post("/api/vms/vm-u20-alpha/restart", u10_hdrs, "", "application/json");
        assert(res1 && res1->status == 403);

        auto res2 = cli.Post("/api/vms/vm-u20-alpha/hibernate", u10_hdrs, "", "application/json");
        assert(res2 && res2->status == 403);

        auto res3 = cli.Post("/api/vms/vm-u20-alpha/wake", u10_hdrs, "", "application/json");
        assert(res3 && res3->status == 403);

        auto res4 = cli.Patch("/api/vms/vm-u20-alpha/ports", u10_hdrs, "{}", "application/json");
        assert(res4 && res4->status == 403);

        auto res5 = cli.Patch("/api/vms/vm-u20-alpha/routing", u10_hdrs, "{\"allow_interproject_routing\": false}", "application/json");
        assert(res5 && res5->status == 403);

        auto res6 = cli.Delete("/api/vms/vm-u20-alpha", u10_hdrs);
        assert(res6 && res6->status == 403);

        auto res7 = cli.Delete(("/api/volumes/" + std::to_string(vol_u20)).c_str(), u10_hdrs);
        assert(res7 && res7->status == 403);
        std::cout << "  [✓] Unauthorized actions on other tenant's resources return HTTP 403 Forbidden\n";
    }

    // 4. Cross-user peering POST /api/links rejected with HTTP 403 Forbidden
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        json peer_body = {{"vm1_id", "vm-u10-alpha"}, {"vm2_id", "vm-u20-alpha"}};
        auto res = cli.Post("/api/links", u10_hdrs, peer_body.dump(), "application/json");
        assert(res && res->status == 403);

        // Admin attempting cross-user peering also rejected with 403
        httplib::Headers admin_hdrs = {{"X-User-ID", "1"}, {"X-Role", "admin"}};
        auto res_admin = cli.Post("/api/links", admin_hdrs, peer_body.dump(), "application/json");
        assert(res_admin && res_admin->status == 403);
        std::cout << "  [✓] Cross-user peering POST /api/links strictly returns HTTP 403 Forbidden\n";
    }

    // 5. Inter-Project Routing Permissions & Validation
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};

        // (a) Same project linking succeeds
        json same_proj_body = {{"vm1_id", "vm-u10-alpha"}, {"vm2_id", "vm-u10-alpha2"}};
        auto res_same = cli.Post("/api/links", u10_hdrs, same_proj_body.dump(), "application/json");
        assert(res_same && res_same->status == 200);

        // (b) Cross-project linking with default allow_interproject_routing = false fails with 400
        json diff_proj_body = {{"vm1_id", "vm-u10-alpha"}, {"vm2_id", "vm-u10-beta"}};
        auto res_diff = cli.Post("/api/links", u10_hdrs, diff_proj_body.dump(), "application/json");
        assert(res_diff && res_diff->status == 400);

        // (c) Toggle routing on vm-u10-alpha via PATCH /api/vms/:uuid/routing
        json toggle_body = {{"allow_interproject_routing", true}};
        auto res_toggle1 = cli.Patch("/api/vms/vm-u10-alpha/routing", u10_hdrs, toggle_body.dump(), "application/json");
        assert(res_toggle1 && res_toggle1->status == 200);

        // Still fails because vm-u10-beta has allow_interproject_routing = false
        auto res_diff2 = cli.Post("/api/links", u10_hdrs, diff_proj_body.dump(), "application/json");
        assert(res_diff2 && res_diff2->status == 400);

        // (d) Toggle routing on vm-u10-beta via PATCH /api/vms/:uuid/ports
        auto res_toggle2 = cli.Patch("/api/vms/vm-u10-beta/ports", u10_hdrs, toggle_body.dump(), "application/json");
        assert(res_toggle2 && res_toggle2->status == 200);

        // Now both have allow_interproject_routing = true -> succeeds!
        auto res_diff3 = cli.Post("/api/links", u10_hdrs, diff_proj_body.dump(), "application/json");
        assert(res_diff3 && res_diff3->status == 200);
        std::cout << "  [✓] Inter-project routing opt-in validation verified (400 when missing, 200 when both opted in)\n";

        // (e) GET /api/links scoping
        auto links_u10 = cli.Get("/api/links", u10_hdrs);
        assert(links_u10 && links_u10->status == 200);
        auto j_links10 = json::parse(links_u10->body);
        assert(j_links10.size() == 2);

        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};
        auto links_u20 = cli.Get("/api/links", u20_hdrs);
        assert(links_u20 && links_u20->status == 200);
        auto j_links20 = json::parse(links_u20->body);
        assert(j_links20.empty());
        std::cout << "  [✓] GET /api/links multi-tenant scoping verified\n";

        // (f) Proactive cross-project revocation via REST API:
        // Toggling allow_interproject_routing to false on vm-u10-alpha must immediately prune the cross-project link
        json revoke_body = {{"allow_interproject_routing", false}};
        auto res_revoke = cli.Patch("/api/vms/vm-u10-alpha/routing", u10_hdrs, revoke_body.dump(), "application/json");
        assert(res_revoke && res_revoke->status == 200);

        auto links_u10_after = cli.Get("/api/links", u10_hdrs);
        assert(links_u10_after && links_u10_after->status == 200);
        auto j_links10_after = json::parse(links_u10_after->body);
        assert(j_links10_after.size() == 1); // Only the same-project link remains!
        std::cout << "  [✓] REST API proactive cross-project link teardown verified\n";

        // (g) Re-attempting to link diff projects now fails again
        auto res_diff_reattempt = cli.Post("/api/links", u10_hdrs, diff_proj_body.dump(), "application/json");
        assert(res_diff_reattempt && res_diff_reattempt->status == 400);
        std::cout << "  [✓] Re-attempting cross-project link after revocation correctly rejected with HTTP 400\n";

        // (h) Payload-based DELETE /api/links
        // User 20 cannot delete User 10's remaining link
        auto res_del_foreign = cli.Delete("/api/links", u20_hdrs, same_proj_body.dump(), "application/json");
        assert(res_del_foreign && res_del_foreign->status == 403);

        // User 10 deletes own remaining same-project link
        auto res_del_own = cli.Delete("/api/links", u10_hdrs, same_proj_body.dump(), "application/json");
        assert(res_del_own && res_del_own->status == 200);

        auto links_u10_empty = cli.Get("/api/links", u10_hdrs);
        assert(links_u10_empty && json::parse(links_u10_empty->body).empty());
        std::cout << "  [✓] Payload-based DELETE /api/links verified with tenant authorization\n";
    }

    // 6. Direct Single-Resource GET Scoping (GET /api/vms/:uuid & GET /api/volumes/:id)
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};
        httplib::Headers admin_hdrs = {{"X-User-ID", "1"}, {"X-Role", "admin"}};

        // User 10 accessing own VM
        auto res_v10 = cli.Get("/api/vms/vm-u10-alpha", u10_hdrs);
        assert(res_v10 && res_v10->status == 200);

        // User 20 accessing User 10's VM -> 403 Forbidden
        auto res_v10_by_u20 = cli.Get("/api/vms/vm-u10-alpha", u20_hdrs);
        assert(res_v10_by_u20 && res_v10_by_u20->status == 403);

        // Admin accessing User 10's VM -> 200 OK
        auto res_v10_by_admin = cli.Get("/api/vms/vm-u10-alpha", admin_hdrs);
        assert(res_v10_by_admin && res_v10_by_admin->status == 200);

        // Non-existent VM -> 404 Not Found
        auto res_v_none = cli.Get("/api/vms/vm-nonexistent", u10_hdrs);
        assert(res_v_none && res_v_none->status == 404);
        std::cout << "  [✓] GET /api/vms/:uuid strictly scoped with 403 for other tenants\n";

        // Volumes
        auto res_vol10 = cli.Get(("/api/volumes/" + std::to_string(vol_u10)).c_str(), u10_hdrs);
        assert(res_vol10 && res_vol10->status == 200);

        auto res_vol10_by_u20 = cli.Get(("/api/volumes/" + std::to_string(vol_u10)).c_str(), u20_hdrs);
        assert(res_vol10_by_u20 && res_vol10_by_u20->status == 403);

        auto res_vol10_by_admin = cli.Get(("/api/volumes/" + std::to_string(vol_u10)).c_str(), admin_hdrs);
        assert(res_vol10_by_admin && res_vol10_by_admin->status == 200);

        auto res_vol_none = cli.Get("/api/volumes/99999", u10_hdrs);
        assert(res_vol_none && res_vol_none->status == 404);
        std::cout << "  [✓] GET /api/volumes/:id strictly scoped with 403 for other tenants\n";
    }

    // 7. Security Edge Cases: Volume Attachment & Auth Headers
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};
        httplib::Headers admin_hdrs = {{"X-User-ID", "1"}, {"X-Role", "admin"}};

        // Cross-tenant volume attachment: User 10 attaching own volume to User 20's VM -> 403
        json attach_foreign = {{"vm_id", "vm-u20-alpha"}};
        auto res_att1 = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), u10_hdrs, attach_foreign.dump(), "application/json");
        assert(res_att1 && res_att1->status == 403);

        // User 20 attaching User 10's volume to User 20's VM -> 403
        auto res_att2 = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), u20_hdrs, attach_foreign.dump(), "application/json");
        assert(res_att2 && res_att2->status == 403);

        // Admin cannot attach across tenants either
        auto res_att3 = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), admin_hdrs, attach_foreign.dump(), "application/json");
        assert(res_att3 && res_att3->status == 403);
        std::cout << "  [✓] Cross-tenant volume attachment strictly forbidden with HTTP 403\n";

        // Missing / Invalid X-User-ID header
        auto res_no_hdr = cli.Get("/api/vms");
        assert(res_no_hdr && res_no_hdr->status == 401);

        httplib::Headers zero_hdr = {{"X-User-ID", "0"}};
        auto res_zero = cli.Get("/api/vms", zero_hdr);
        assert(res_zero && res_zero->status == 400);

        httplib::Headers neg_hdr = {{"X-User-ID", "-3"}};
        auto res_neg = cli.Get("/api/vms", neg_hdr);
        assert(res_neg && res_neg->status == 400);

        // Large integer on volume delete does not crash
        auto res_huge = cli.Delete("/api/volumes/99999999999999999999999999", u10_hdrs);
        assert(res_huge && (res_huge->status == 400 || res_huge->status == 404));

        // Self-linking via API returns 400
        json self_link = {{"vm1_id", "vm-u10-alpha"}, {"vm2_id", "vm-u10-alpha"}};
        auto res_self = cli.Post("/api/links", u10_hdrs, self_link.dump(), "application/json");
        assert(res_self && res_self->status == 400);
        std::cout << "  [✓] Authentication and payload edge cases successfully verified\n";
    }

    // 8. REST API Edge Cases: Unlinking destroyed VMs, volume auto-detachment, and error payloads
    {
        httplib::Headers u10_hdrs = {{"X-User-ID", "10"}, {"X-Role", "user"}};
        httplib::Headers u20_hdrs = {{"X-User-ID", "20"}, {"X-Role", "user"}};

        // (a) Opt-in and create cross-project link between vm-u10-alpha and vm-u10-beta
        json toggle_true = {{"allow_interproject_routing", true}};
        cli.Patch("/api/vms/vm-u10-alpha/routing", u10_hdrs, toggle_true.dump(), "application/json");
        cli.Patch("/api/vms/vm-u10-beta/routing", u10_hdrs, toggle_true.dump(), "application/json");
        json link_cross = {{"vm1_id", "vm-u10-alpha"}, {"vm2_id", "vm-u10-beta"}};
        auto res_link_cross = cli.Post("/api/links", u10_hdrs, link_cross.dump(), "application/json");
        assert(res_link_cross && res_link_cross->status == 200);

        // Destroy vm-u10-beta
        auto res_del_vm = cli.Delete("/api/vms/vm-u10-beta", u10_hdrs);
        assert(res_del_vm && res_del_vm->status == 200);

        // Attempting to delete link when one VM is destroyed:
        // Unauthorized tenant (User 20) attempting to delete link -> 403 Forbidden
        auto res_unlink_by_u20 = cli.Delete("/api/links/vm-u10-alpha/vm-u10-beta", u20_hdrs);
        assert(res_unlink_by_u20 && res_unlink_by_u20->status == 403);

        // Payload-based delete by unauthorized tenant -> 403 Forbidden
        auto res_unlink_body_u20 = cli.Delete("/api/links", u20_hdrs, link_cross.dump(), "application/json");
        assert(res_unlink_body_u20 && res_unlink_body_u20->status == 403);

        // Surviving VM's owner (User 10) unlinks via path -> 200 OK
        auto res_unlink_by_u10 = cli.Delete("/api/links/vm-u10-alpha/vm-u10-beta", u10_hdrs);
        assert(res_unlink_by_u10 && res_unlink_by_u10->status == 200);

        // Surviving VM's owner (User 10) unlinks via payload -> 200 OK
        auto res_unlink_body_u10 = cli.Delete("/api/links", u10_hdrs, link_cross.dump(), "application/json");
        assert(res_unlink_body_u10 && res_unlink_body_u10->status == 200);

        // Neither VM exists -> 404 Not Found
        auto res_unlink_none = cli.Delete("/api/links/vm-none1/vm-none2", u10_hdrs);
        assert(res_unlink_none && res_unlink_none->status == 404);

        // Self-unlinking -> 400 Bad Request
        auto res_unlink_self = cli.Delete("/api/links/vm-u10-alpha/vm-u10-alpha", u10_hdrs);
        assert(res_unlink_self && res_unlink_self->status == 400);

        // (b) Malformed and non-object JSON payloads return 400 Bad Request
        auto res_bad_json1 = cli.Post("/api/links", u10_hdrs, "invalid-json", "application/json");
        assert(res_bad_json1 && res_bad_json1->status == 400);

        auto res_bad_json2 = cli.Patch("/api/vms/vm-u10-alpha/ports", u10_hdrs, "invalid-json", "application/json");
        assert(res_bad_json2 && res_bad_json2->status == 400);

        auto res_bad_json3 = cli.Post("/api/volumes", u10_hdrs, "invalid-json", "application/json");
        assert(res_bad_json3 && res_bad_json3->status == 400);

        auto res_bad_json4 = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), u10_hdrs, "invalid-json", "application/json");
        assert(res_bad_json4 && res_bad_json4->status == 400);

        auto res_non_obj1 = cli.Post("/api/links", u10_hdrs, "[1, 2, 3]", "application/json");
        assert(res_non_obj1 && res_non_obj1->status == 400);

        auto res_non_obj2 = cli.Post("/api/volumes", u10_hdrs, "\"string-not-obj\"", "application/json");
        assert(res_non_obj2 && res_non_obj2->status == 400);

        auto res_bad_vol_id = cli.Post("/api/volumes/999999999999999999999/attach", u10_hdrs, "{}", "application/json");
        assert(res_bad_vol_id && res_bad_vol_id->status == 400);

        // (c) Volume detachment when VM is destroyed
        // Attach vol_u10 to vm-u10-alpha
        json attach_body = {{"vm_id", "vm-u10-alpha"}};
        auto res_att = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), u10_hdrs, attach_body.dump(), "application/json");
        assert(res_att && res_att->status == 200);

        // Verify attached
        auto res_vol_chk = cli.Get(("/api/volumes/" + std::to_string(vol_u10)).c_str(), u10_hdrs);
        assert(res_vol_chk && res_vol_chk->status == 200);
        auto j_vol = json::parse(res_vol_chk->body);
        assert(j_vol["attached_vm_uuid"] == "vm-u10-alpha");

        // Destroy vm-u10-alpha
        auto res_del_alpha = cli.Delete("/api/vms/vm-u10-alpha", u10_hdrs);
        assert(res_del_alpha && res_del_alpha->status == 200);

        // Volume should now be automatically detached
        auto res_vol_after = cli.Get(("/api/volumes/" + std::to_string(vol_u10)).c_str(), u10_hdrs);
        assert(res_vol_after && res_vol_after->status == 200);
        auto j_vol_after = json::parse(res_vol_after->body);
        assert(j_vol_after["attached_vm_id"] == -1);

        // (d) Terminating VM lifecycle operations rejection
        MicroVM v_term_api;
        v_term_api.id = 77; v_term_api.uuid = "vm-u10-terminating"; v_term_api.owner_id = 10;
        v_term_api.project_name = "project-A"; v_term_api.status = "terminating";
        manager.add_vm_for_test(v_term_api);

        auto res_term_restart = cli.Post("/api/vms/vm-u10-terminating/restart", u10_hdrs);
        assert(res_term_restart && res_term_restart->status == 400);

        auto res_term_wake = cli.Post("/api/vms/vm-u10-terminating/wake", u10_hdrs);
        assert(res_term_wake && res_term_wake->status == 400);

        auto res_term_hib = cli.Post("/api/vms/vm-u10-terminating/hibernate", u10_hdrs);
        assert(res_term_hib && res_term_hib->status == 400);

        json ports_body = {{"ssh_exposed", true}};
        auto res_term_ports = cli.Patch("/api/vms/vm-u10-terminating/ports", u10_hdrs, ports_body.dump(), "application/json");
        assert(res_term_ports && res_term_ports->status == 400);

        json routing_body = {{"allow_interproject_routing", true}};
        auto res_term_rout = cli.Patch("/api/vms/vm-u10-terminating/routing", u10_hdrs, routing_body.dump(), "application/json");
        assert(res_term_rout && res_term_rout->status == 400);

        json att_term_body = {{"vm_id", "vm-u10-terminating"}};
        auto res_term_att = cli.Post(("/api/volumes/" + std::to_string(vol_u10) + "/attach").c_str(), u10_hdrs, att_term_body.dump(), "application/json");
        assert(res_term_att && res_term_att->status == 400);

        std::cout << "  [✓] Deep verification of destroyed VM unlinking, detachment, and payload validation passed\n";
    }

    svr.stop();
    server_thread.join();

    unlink(test_db.c_str());
    unlink((test_db + "-wal").c_str());
    unlink((test_db + "-shm").c_str());
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "   CryoSpawn Deep Verification Test Suite \n";
    std::cout << "==========================================\n";
    test_database_and_slots();
    test_ghost_proxy_tcp();
    test_multitenant_and_interproject_routing_logic();
    test_api_server_multitenancy_and_interproject();
    std::cout << "==========================================\n";
    std::cout << " [✓] ALL DEEP VERIFICATION TESTS PASSED!  \n";
    std::cout << "==========================================\n";
    return 0;
}
