#pragma once
#include <string>
#include <iostream>
#include <cstdlib>
#include <array>

// Helper to run a shell command and capture its output (like 'ip route')
std::string get_default_interface() {
    std::array<char, 128> buffer;
    std::string result;
    
    // Open pipe to run shell command
    FILE* pipe = popen("ip route show default | awk '{print $5}'", "r");
    if (!pipe) {
        return "";
    }
    
    // Read the output
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    // Close the pipe cleanly
    pclose(pipe);
    
    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    return result;
}

bool setup_cryo_network(const std::string& tap_name, const std::string& host_ip, const std::string& out_iface) {
    std::cout << "[*] Configuring network on " << tap_name << " (NAT out via " << out_iface << ")...\n";
    
    system(("sudo ip tuntap add " + tap_name + " mode tap").c_str());
    system(("sudo ip addr add " + host_ip + "/24 dev " + tap_name).c_str());
    system(("sudo ip link set " + tap_name + " up").c_str());

    system("sudo sysctl -w net.ipv4.ip_forward=1 > /dev/null");

    system(("sudo iptables -t nat -A POSTROUTING -o " + out_iface + " -j MASQUERADE").c_str());
    system("sudo iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT");
    system(("sudo iptables -A FORWARD -i " + tap_name + " -o " + out_iface + " -j ACCEPT").c_str());

    return true;
}

void teardown_cryo_network(const std::string& tap_name) {
    std::cout << "[*] Tearing down network " << tap_name << "...\n";
    system(("sudo ip link set " + tap_name + " down").c_str());
    system(("sudo ip tuntap del " + tap_name + " mode tap").c_str());
}

std::string get_lan_ip(const std::string& iface) {
    std::array<char, 128> buffer;
    std::string result;
    // Extracts the IP address of the active Wi-Fi/Ethernet interface
    std::string cmd = "ip -4 addr show " + iface + " | awk '/inet / {print $2}' | cut -d/ -f1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "127.0.0.1";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) { result += buffer.data(); }
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

void expose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    std::cout << "[*] Exposing VM " << vm_ip << ":" << vm_port << " to host port " << host_port << "\n";
    std::string cmd = "sudo iptables -t nat -A PREROUTING -i " + out_iface + 
                      " -p tcp --dport " + std::to_string(host_port) + 
                      " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port);
    system(cmd.c_str());
}

void unexpose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    std::string cmd = "sudo iptables -t nat -D PREROUTING -i " + out_iface + 
                      " -p tcp --dport " + std::to_string(host_port) + 
                      " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port);
    system(cmd.c_str());
}

