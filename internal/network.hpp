#pragma once
#include <string>
#include <iostream>
#include <cstdlib>
#include <array>


void run_idempotent_rule(const std::string& rule_args) {
    // Check if the exact rule already exists
    std::string check_cmd = "iptables -C " + rule_args + " 2>/dev/null";
    if (system(check_cmd.c_str()) != 0) {
        // If not found, append it cleanly
        std::string add_cmd = "iptables -A " + rule_args;
        system(add_cmd.c_str());
    }
}

void init_cryo_firewall_baseline(const std::string& out_iface) {
    std::cout << "[*] Initializing Zero-Trust firewall baseline...\n";

    // Enable IP forwarding globally
    system("sysctl -w net.ipv4.ip_forward=1 > /dev/null");

    // 1. NAT Masquerade out to the internet
    run_idempotent_rule("POSTROUTING -t nat -o " + out_iface + " -j MASQUERADE");

    // 2. Allow established and related traffic
    run_idempotent_rule("FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT");

    // 3. Allow DNAT / hairpin traffic between cryo interfaces (public endpoint access)
    run_idempotent_rule("FORWARD -i cryo+ -o cryo+ -m conntrack --ctstate DNAT -j ACCEPT");

    // 4. Drop un-NATed internal cross-talk between isolated subnets
    run_idempotent_rule("FORWARD -i cryo+ -o cryo+ -d 172.16.0.0/16 -j DROP");

    // 5. Drop outbound access to the physical host's private LAN
    run_idempotent_rule("FORWARD -i cryo+ -d 192.168.0.0/16 -j DROP");
    run_idempotent_rule("FORWARD -i cryo+ -d 10.0.0.0/8 -j DROP");

    // 6. Seal off the hypervisor (Host Laptop) from the VMs
    run_idempotent_rule("INPUT -i cryo+ -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT");
    run_idempotent_rule("INPUT -i cryo+ -j DROP");
}

std::string get_default_interface() {
    std::array<char, 128> buffer; std::string result;
    FILE* pipe = popen("ip route show default | awk '{print $5}'", "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

bool setup_cryo_network(const std::string& tap_name, const std::string& host_ip, const std::string& out_iface) {
    std::cout << "[*] Configuring network on " << tap_name << " (NAT out via " << out_iface << ")...\n";
    system(("ip tuntap add " + tap_name + " mode tap").c_str());
    system(("ip addr add " + host_ip + "/30 dev " + tap_name).c_str());
    system(("ip link set " + tap_name + " up").c_str());
    system("sysctl -w net.ipv4.ip_forward=1 > /dev/null");
    system(("iptables -A FORWARD -i " + tap_name + " -o " + out_iface + " -j ACCEPT").c_str());
    return true;
}

void teardown_cryo_network(const std::string& tap_name) {
    std::cout << "[*] Tearing down network " << tap_name << "...\n";
    system(("ip link set " + tap_name + " down").c_str());
    system(("ip tuntap del " + tap_name + " mode tap").c_str());
}

std::string get_lan_ip(const std::string& iface) {
    std::array<char, 128> buffer; std::string result;
    std::string cmd = "ip -4 addr show " + iface + " | awk '/inet / {print $2}' | cut -d/ -f1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "127.0.0.1";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

void expose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    std::string cmd = "iptables -t nat -A PREROUTING -i " + out_iface + " -p tcp --dport " + std::to_string(host_port) + " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port);
    system(cmd.c_str());
}

void unexpose_vm_port(const std::string& out_iface, int host_port, const std::string& vm_ip, int vm_port) {
    std::string cmd = "iptables -t nat -D PREROUTING -i " + out_iface + " -p tcp --dport " + std::to_string(host_port) + " -j DNAT --to-destination " + vm_ip + ":" + std::to_string(vm_port);
    system(cmd.c_str());
}

void link_taps(const std::string& tap1, const std::string& tap2) {
    std::cout << "[*] Bridging network: " << tap1 << " <---> " << tap2 << "\n";
    
    // 1. Allow bidirectional routing at the very top of the firewall
    system(("iptables -I FORWARD 1 -i " + tap1 + " -o " + tap2 + " -j ACCEPT").c_str());
    system(("iptables -I FORWARD 1 -i " + tap2 + " -o " + tap1 + " -j ACCEPT").c_str());
    
    // 2. Add Source NAT (Masquerade) to bypass guest OS strict subnet drops
    system(("iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o " + tap2 + " -j MASQUERADE").c_str());
    system(("iptables -t nat -I POSTROUTING 1 -s 172.16.0.0/16 -o " + tap1 + " -j MASQUERADE").c_str());
}

void unlink_taps(const std::string& tap1, const std::string& tap2) {
    std::cout << "[*] Severing network: " << tap1 << " -X- " << tap2 << "\n";
    
    system(("iptables -D FORWARD -i " + tap1 + " -o " + tap2 + " -j ACCEPT 2>/dev/null").c_str());
    system(("iptables -D FORWARD -i " + tap2 + " -o " + tap1 + " -j ACCEPT 2>/dev/null").c_str());
    
    system(("iptables -t nat -D POSTROUTING -s 172.16.0.0/16 -o " + tap2 + " -j MASQUERADE 2>/dev/null").c_str());
    system(("iptables -t nat -D POSTROUTING -s 172.16.0.0/16 -o " + tap1 + " -j MASQUERADE 2>/dev/null").c_str());
}