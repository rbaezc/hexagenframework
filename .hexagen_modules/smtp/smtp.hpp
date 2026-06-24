// Self-contained simple SMTP client in C++
#pragma once
#include <string>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <algorithm>

class SMTPClient {
public:
    static bool sendEmail(const std::string& host, int port, const std::string& from, const std::string& to, const std::string& subject, const std::string& body) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) { close(sock); return false; }
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        std::copy((char*)server->h_addr, (char*)server->h_addr + server->h_length, (char*)&addr.sin_addr.s_addr);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return false; }
        
        auto read_resp = [sock]() {
            char buf[1024];
            int n = recv(sock, buf, 1023, 0);
            if (n > 0) { buf[n] = '\0'; }
        };
        
        auto send_cmd = [sock, read_resp](const std::string& cmd) {
            send(sock, cmd.c_str(), cmd.length(), 0);
            read_resp();
        };

        read_resp();
        send_cmd("HELO localhost\r\n");
        send_cmd("MAIL FROM:<" + from + ">\r\n");
        send_cmd("RCPT TO:<" + to + ">\r\n");
        send_cmd("DATA\r\n");
        std::stringstream msg;
        msg << "From: " << from << "\r\n"
            << "To: " << to << "\r\n"
            << "Subject: " << subject << "\r\n\r\n"
            << body << "\r\n.\r\n";
        send_cmd(msg.str());
        send_cmd("QUIT\r\n");
        close(sock);
        return true;
    }
};
