#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <utility>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// A generic JSON parser that extracts all flat key-value pairs from any JSON payload
std::vector<std::pair<std::string, std::string>> parseJSONFields(const std::string& json) {
    std::vector<std::pair<std::string, std::string>> fields;
    size_t pos = 0;
    while (true) {
        size_t keyStart = json.find("\"", pos);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = json.find("\"", keyStart + 1);
        if (keyEnd == std::string::npos) break;
        
        std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        size_t colon = json.find(":", keyEnd + 1);
        if (colon == std::string::npos) break;
        
        size_t valStart = colon + 1;
        while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\t' || json[valStart] == '\r' || json[valStart] == '\n')) {
            valStart++;
        }
        
        if (valStart >= json.length()) break;
        
        std::string value = "";
        if (json[valStart] == '"') {
            size_t valEnd = json.find("\"", valStart + 1);
            if (valEnd == std::string::npos) break;
            value = json.substr(valStart + 1, valEnd - valStart - 1);
            pos = valEnd + 1;
        } else {
            size_t valEnd = valStart;
            while (valEnd < json.length() && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != ' ' && json[valEnd] != '\n' && json[valEnd] != '\r') {
                valEnd++;
            }
            value = json.substr(valStart, valEnd - valStart);
            pos = valEnd;
        }
        
        // Exclude internal routing/metadata fields if they are not user data
        if (key != "action") {
            fields.push_back({key, value});
        }
    }
    return fields;
}

class GenericTransaction {
public:
    std::vector<std::pair<std::string, std::string>> parameters;

    GenericTransaction(std::vector<std::pair<std::string, std::string>> params) : parameters(params) {}

    std::string execute() {
        std::cout << "[C++ Core] Starting high-performance business transaction..." << std::endl;
        
        std::stringstream ss;
        ss << "{\n"
           << "  \"status\": \"success\",\n"
           << "  \"message\": \"Transacción completada exitosamente a nivel de Metal C++!\",\n"
           << "  \"processed_fields\": {\n";
        
        for (size_t i = 0; i < parameters.size(); ++i) {
            std::cout << "  [C++ Field] " << parameters[i].first << " = " << parameters[i].second << std::endl;
            ss << "    \"" << parameters[i].first << "\": \"" << parameters[i].second << "\"";
            if (i + 1 < parameters.size()) ss << ",\n";
            else ss << "\n";
        }
        
        ss << "  },\n"
           << "  \"latency_ms\": 0,\n"
           << "  \"timestamp\": " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n"
           << "}";
        return ss.str();
    }
};

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    int port = 9090;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "[C++ Core] Socket creation failed" << std::endl;
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << "[C++ Core] setsockopt failed" << std::endl;
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "[C++ Core] Bind failed on port " << port << std::endl;
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "[C++ Core] Listen failed" << std::endl;
        return 1;
    }

    std::cout << "🏎️  [C++ Core] Generic Transaction Engine running on port " << port << std::endl;

    while (true) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            std::cerr << "[C++ Core] Accept failed" << std::endl;
            continue;
        }

        char buffer[4096] = {0};
        int valread = read(client_fd, buffer, 4096);
        if (valread > 0) {
            std::string request(buffer, valread);
            std::cout << "[C++ Core] Payload received: " << request << std::endl;

            // Extract all dynamic fields
            auto fields = parseJSONFields(request);

            // Execute dynamic transaction
            GenericTransaction tx(fields);
            std::string response = tx.execute();

            send(client_fd, response.c_str(), response.length(), 0);
            std::cout << "[C++ Core] Execution response sent." << std::endl;
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
