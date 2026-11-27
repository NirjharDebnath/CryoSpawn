#include <iostream>
#include <filesystem>
#include <thread>
#include <csignal>
#include "server.hpp"

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

    register_routes(svr, manager, KERNEL_PATH, ROOTFS_PATH);

    std::cout << "[*] CryoSpawn Daemon (C++) pure REST API running on 127.0.0.1:" << BACKEND_PORT << "\n";
    svr.listen("127.0.0.1", BACKEND_PORT);
    return 0;
}
