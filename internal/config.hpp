#pragma once
#include <string>
#include <filesystem>
#include <unistd.h>
#include <limits.h>

namespace config {
    // Dynamically resolves the absolute path to the CryoSpawn project root.
    // It finds the location of the `cryospawn` binary being executed and 
    // sets the base path accordingly, regardless of where you run it from!
    inline std::string get_base_path() {
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
}


