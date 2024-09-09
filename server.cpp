#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct HttpResponse {
    std::string version;
    std::string status;
    std::string statusString;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::string makeHttpResponse(const HttpResponse& response) {
    std::ostringstream out;
    const char* sep = " ";
    const char* crlf = "\r\n";
    out << response.version << sep << response.status << sep << response.statusString << crlf;
    for (const auto& header : response.headers) {
        out << header.first << ": " << header.second << crlf;
    }
    out << crlf << response.body;
    return out.str();
}

HttpRequest parseHttpRequest(const std::string& request) {
    HttpRequest httpRequest;
    std::istringstream requestStream(request);
    std::string line;
    
    // Parse the request line
    std::getline(requestStream, line);
    std::istringstream lineStream(line);
    lineStream >> httpRequest.method >> httpRequest.path >> httpRequest.version;

    // Parse headers
    while (std::getline(requestStream, line) && line != "\r") {
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string headerName = line.substr(0, colonPos);
            std::string headerValue = line.substr(colonPos + 2);
            httpRequest.headers[headerName] = headerValue;
        }
    }

    // Parse body
    while (std::getline(requestStream, line)) {
        httpRequest.body += line + "\n";
    }
    return httpRequest;
}

long readFile(const std::string& filePath, std::string& content) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::ifstream::pos_type fileSize = file.tellg();
        content.resize(fileSize);
        file.seekg(0, std::ios::beg);
        file.read(&content[0], fileSize);
        file.close();
        return fileSize;
    }
    return -1; // File not found
}

void handle_client(int socket_fd, const std::string& directory) {
    std::string request;
    char buffer[4096];
    ssize_t bytes_received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        std::cerr << "Failed to receive data\n";
        close(socket_fd);
        return;
    }
    buffer[bytes_received] = '\0';
    request = buffer;

    HttpRequest httpRequest = parseHttpRequest(request);
    HttpResponse response;
    response.version = "HTTP/1.1";

    std::string filePath = directory + httpRequest.path;
    if (httpRequest.method == "GET") {
        if (fs::exists(filePath)) {
            std::string respBody;
            long fileSize = readFile(filePath, respBody);
            response.status = "200";
            response.statusString = "OK";
            response.headers["Content-Length"] = std::to_string(fileSize);
            response.headers["Content-Type"] = "application/octet-stream";
            response.body = respBody;
        } else {
            response.status = "404";
            response.statusString = "Not Found";
        }
    } else if (httpRequest.method == "POST") {
        std::ofstream outfile(filePath);
        outfile << httpRequest.body;
        outfile.close();
        response.status = "201";
        response.statusString = "Created";
    } else {
        response.status = "405";
        response.statusString = "Method Not Allowed";
    }

    std::string http_response = makeHttpResponse(response);
    ssize_t bytes_sent = send(socket_fd, http_response.c_str(), http_response.size(), 0);
    if (bytes_sent < 0) {
        std::cerr << "Failed to send data\n";
    }
    close(socket_fd);
}

int main(int argc, char **argv) {
    if (argc != 3 || std::strcmp(argv[1], "--directory") != 0) {
        std::cerr << "Usage: " << argv[0] << " --directory <directory_path>\n";
        return 1;
    }

    std::string directory = argv[2];
    std::cout << "Serving files from directory: " << directory << "\n";

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    std::cout << "Waiting for a client to connect...\n";
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }
        std::thread(handle_client, client_fd, directory).detach();
    }

    close(server_fd);
    return 0;
}
