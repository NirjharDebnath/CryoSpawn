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
        db.insert_vm(0, 2001, "stopped", 1, 512, "172.16.0.2", "172.16.0.1", "cryo-test0", false, 0, "user2-proj", false, false, 3, "vm-user2vm", "u2-vm", 2);
        
        // User 2 should see only 1 VM (slot 0)
        auto u2_vms = db.get_all_vms(2, false);
        assert(u2_vms.size() == 1);
        assert(u2_vms[0].slot == 0);
        assert(u2_vms[0].owner_id == 2);

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

int main() {
    std::cout << "==========================================\n";
    std::cout << "   CryoSpawn Deep Verification Test Suite \n";
    std::cout << "==========================================\n";
    test_database_and_slots();
    test_ghost_proxy_tcp();
    std::cout << "==========================================\n";
    std::cout << " [✓] ALL DEEP VERIFICATION TESTS PASSED!  \n";
    std::cout << "==========================================\n";
    return 0;
}
