#pragma once
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

bool is_http_success(const std::string& response) {
    if (response.rfind("HTTP/1.", 0) == 0 && response.length() >= 12) {
        try {
            int code = std::stoi(response.substr(9, 3));
            return (code >= 200 && code < 300);
        } catch (...) { return false; }
    }
    return false;
}

bool send_firecracker_put(const std::string& socket_path, const std::string& endpoint, const std::string& json_body) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    std::string request = 
        "PUT " + endpoint + " HTTP/1.1\r\n" +
        "Host: localhost\r\n" +
        "Accept: application/json\r\n" +
        "Content-Type: application/json\r\n" +
        "Content-Length: " + std::to_string(json_body.length()) + "\r\n\r\n" +
        json_body;

    send(sock, request.c_str(), request.length(), 0);

    char buffer[2048] = {0};
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);

    if (bytes_read > 0) {
        std::string response(buffer);
        if (is_http_success(response)) {
            std::cout << "[+] API OK: " << endpoint << "\n";
            return true;
        } else {
            std::cerr << "[-] API Failed " << endpoint << ":\n" << response << "\n";
        }
    }
    return false;
}