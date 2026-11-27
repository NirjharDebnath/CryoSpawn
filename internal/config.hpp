#pragma once
#include <string>
#include <filesystem>
#include <unistd.h>
#include <limits.h>
#include <cstdlib>
#include <cstring>

namespace config {
    // Dynamically resolves the absolute path to the CryoSpawn project root.
    // It finds the location of the `cryospawn` binary being executed and 
    // sets the base path accordingly, regardless of where you run it from!
    inline std::string get_base_path() {
        const char* env_base = std::getenv("CRYOSPAWN_BASE_PATH");
        if (env_base && std::strlen(env_base) > 0) {
            return std::string(env_base);
        }

        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        if (count == -1) return ".";
        
        std::filesystem::path exe_path(std::string(result, count));
        std::filesystem::path dir = exe_path.parent_path();
        
        // If the user compiled the binary inside `internal/`, the project root is one level up.
        if (dir.filename() == "internal") {
            return dir.parent_path().string();
        }
        
        // Otherwise, assume the binary is in the project root.
        return dir.string();
    }
    
    inline const std::string BASE_PATH = get_base_path();

    inline std::string get_kernel_path() {
        const char* env = std::getenv("CRYOSPAWN_KERNEL_PATH");
        if (env && std::strlen(env) > 0) return std::string(env);
        return BASE_PATH + "/vmlinux/ubuntu-vmlinux.bin";
    }

    inline std::string get_rootfs_path() {
        const char* env = std::getenv("CRYOSPAWN_ROOTFS_PATH");
        if (env && std::strlen(env) > 0) return std::string(env);
        return BASE_PATH + "/rootfs/ubuntu-rootfs.ext4";
    }

    inline std::string get_db_path() {
        const char* env = std::getenv("CRYOSPAWN_DB_PATH");
        if (env && std::strlen(env) > 0) return std::string(env);
        return BASE_PATH + "/cryospawn.db";
    }

    inline int get_backend_port() {
        const char* env = std::getenv("CRYOSPAWN_BACKEND_PORT");
        if (env && std::strlen(env) > 0) {
            try { return std::stoi(env); } catch (...) {}
        }
        return 9090;
    }
}

