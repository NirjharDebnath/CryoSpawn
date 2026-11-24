#pragma once
#include <string>
#include <iostream>
#include <cstdlib>
#include <array>
#include <vector>
#include <unordered_set>
#include <unistd.h>

inline bool is_running_as_root() {
    return (geteuid() == 0);
}

inline void run_idempotent_rule(const std::string& rule_args) {
    if (!is_running_as_root()) return;
    std::string check_cmd = "iptables -C " + rule_args + " 2>/dev/null";
    if (system(check_cmd.c_str()) != 0) {
        std::string add_cmd = "iptables -A " + rule_args + " 2>/dev/null";
        system(add_cmd.c_str());
    }
}

inline void init_cryo_firewall_baseline(const std::string& out_iface) {
    if (!is_running_as_root()) {
        std::cout << "[!] CryoSpawn is running without root privileges (UID " << geteuid() 
                  << "). TAP device creation and iptables firewall rules require root.\n";
        return;
    }

    std::cout << "[*] Initializing Zero-Trust firewall baseline...\n";

    // Enable IP forwarding globally
    system("sysctl -w net.ipv4.ip_forward=1 > /dev/null 2>&1");

    // 1. NAT Masquerade out to the internet
    if (!out_iface.empty()) {
        run_idempotent_rule("POSTROUTING -t nat -o " + out_iface + " -j MASQUERADE");
    }

    // 2. Allow established and related traffic
    run_idempotent_rule("FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT");

    // 3. Allow DNAT / hairpin traffic between cryo interfaces
    run_idempotent_rule("FORWARD -i cryo+ -o cryo+ -m conntrack --ctstate DNAT -j ACCEPT");

    // 4. Drop un-NATed internal cross-talk between isolated subnets
    run_idempotent_rule("FORWARD -i cryo+ -o cryo+ -d 172.16.0.0/16 -j DROP");

    // 5. Drop outbound access to physical host's private LAN (fail fast with REJECT)
    run_idempotent_rule("FORWARD -i cryo+ -d 192.168.0.0/16 -j REJECT --reject-with icmp-port-unreachable");
    run_idempotent_rule("FORWARD -i cryo+ -d 10.0.0.0/8 -j REJECT --reject-with icmp-port-unreachable");

    // 6. Seal off the hypervisor host from the VMs
    run_idempotent_rule("INPUT -i cryo+ -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT");
    run_idempotent_rule("INPUT -i cryo+ -j REJECT --reject-with icmp-port-unreachable");
}

inline std::string get_default_interface() {
    std::array<char, 128> buffer; std::string result;
    FILE* pipe = popen("ip route show default 2>/dev/null | awk '{print $5}'", "r");
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) result += buffer.data();
        pclose(pipe);
    }
    if (!result.empty() && result.back() == '\n') result.pop_back();

    // Fallback: check first active non-loopback interface
    if (result.empty()) {
        FILE* p2 = popen("ip -o link show up 2>/dev/null | awk -F': ' '{print $2}' | grep -v '^lo$' | head -n1", "r");
        if (p2) {
            while (fgets(buffer.data(), buffer.size(), p2) != nullptr) result += buffer.data();
            pclose(p2);
            if (!result.empty() && result.back() == '\n') result.pop_back();
        }
    }
    return result;
}

inline bool setup_cryo_network(const std::string& tap_name, const std::string& host_ip, const std::string& out_iface) {
    if (!is_running_as_root()) return false;
    std::cout << "[*] Configuring network on " << tap_name << (out_iface.empty() ? "" : (" (NAT out via " + out_iface + ")")) << "...\n";
    system(("ip tuntap add " + tap_name + " mode tap >/dev/null 2>&1").c_str());
    system(("ip addr add " + host_ip + "/30 dev " + tap_name + " >/dev/null 2>&1").c_str());
    system(("ip link set " + tap_name + " up >/dev/null 2>&1").c_str());
    system("sysctl -w net.ipv4.ip_forward=1 > /dev/null 2>&1");
    if (!out_iface.empty()) {
        system(("iptables -A FORWARD -i " + tap_name + " -o " + out_iface + " -j ACCEPT >/dev/null 2>&1").c_str());
    }
    return true;
}

inline void teardown_cryo_network(const std::string& tap_name, const std::string& out_iface = "") {
    if (!is_running_as_root()) return;
    std::cout << "[*] Tearing down network " << tap_name << "...\n";
    if (!out_iface.empty()) {
        system(("iptables -D FORWARD -i " + tap_name + " -o " + out_iface + " -j ACCEPT >/dev/null 2>&1").c_str());
    }
    system(("iptables -D FORWARD -i " + tap_name + " -j ACCEPT >/dev/null 2>&1").c_str());
    system(("iptables -D FORWARD -o " + tap_name + " -j ACCEPT >/dev/null 2>&1").c_str());
    system(("ip link set " + tap_name + " down >/dev/null 2>&1").c_str());
    system(("ip tuntap del " + tap_name + " mode tap >/dev/null 2>&1").c_str());
}

inline void cleanup_orphaned_taps(const std::unordered_set<std::string>& active_tap_names) {
    if (!is_running_as_root()) return;
    FILE* pipe = popen("ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | grep '^cryo-'", "r");
    if (!pipe) return;
    std::array<char, 128> buffer;
    std::vector<std::string> existing_taps;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        std::string tap = buffer.data();
        while (!tap.empty() && (tap.back() == '\n' || tap.back() == '\r' || tap.back() == ' ')) tap.pop_back();
        if (!tap.empty()) existing_taps.push_back(tap);
    }
    pclose(pipe);

    for (const auto& tap : existing_taps) {
        if (active_tap_names.find(tap) == active_tap_names.end()) {
            std::cout << "[*] Cleaning up orphaned TAP interface: " << tap << "\n";
            teardown_cryo_network(tap);
        }
    }
}

inline std::string get_lan_ip(const std::string& iface) {
    if (iface.empty()) return "127.0.0.1";
    std::array<char, 128> buffer; std::string result;
    std::string cmd = "ip -4 addr show " + iface + " 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "127.0.0.1";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result.empty() ? "127.0.0.1" : result;
}

inline void expose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    if (!is_running_as_root() || out_iface.empty() || host_port <= 0 || host_port > 65535 || vm_port <= 0 || vm_port > 65535) return;
    std::string cmd = "iptables -t nat -A PREROUTING -i " + out_iface + " -p tcp --dport " + std::to_string(host_port) + " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port);
    system(cmd.c_str());
}

inline void unexpose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    if (!is_running_as_root() || out_iface.empty() || host_port <= 0 || host_port > 65535 || vm_port <= 0 || vm_port > 65535) return;
    std::string cmd = "iptables -t nat -D PREROUTING -i " + out_iface + " -p tcp --dport " + std::to_string(host_port) + " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port) + " 2>/dev/null";
    system(cmd.c_str());
}

inline void link_taps(const std::string& tap1, const std::string& tap2) {
    if (!is_running_as_root()) return;
    std::cout << "[*] Bridging network: " << tap1 << " <---> " << tap2 << "\n";
    
    // 1. Allow bidirectional routing at the very top of the firewall
    system(("iptables -I FORWARD 1 -i " + tap1 + " -o " + tap2 + " -j ACCEPT 2>/dev/null").c_str());
    system(("iptables -I FORWARD 1 -i " + tap2 + " -o " + tap1 + " -j ACCEPT 2>/dev/null").c_str());
    
    // 2. Add Source NAT (Masquerade) to bypass guest OS strict subnet drops
    system(("iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o " + tap2 + " -j MASQUERADE 2>/dev/null").c_str());
    system(("iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o " + tap1 + " -j MASQUERADE 2>/dev/null").c_str());
}

inline void unlink_taps(const std::string& tap1, const std::string& tap2) {
    if (!is_running_as_root()) return;
    std::cout << "[*] Severing network: " << tap1 << " -X- " << tap2 << "\n";
    
    system(("iptables -D FORWARD -i " + tap1 + " -o " + tap2 + " -j ACCEPT 2>/dev/null").c_str());
    system(("iptables -D FORWARD -i " + tap2 + " -o " + tap1 + " -j ACCEPT 2>/dev/null").c_str());
    
    system(("iptables -t nat -D POSTROUTING -s 172.16.0.0/16 -o " + tap2 + " -j MASQUERADE 2>/dev/null").c_str());
    system(("iptables -t nat -D POSTROUTING -s 172.16.0.0/16 -o " + tap1 + " -j MASQUERADE 2>/dev/null").c_str());
}
