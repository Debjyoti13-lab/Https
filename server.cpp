#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring> // Include this header for strcmp

#pragma comment(lib, "Ws2_32.lib")

std::string directory;

void handle_client(SOCKET client_socket) {
    char buffer[1024];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == SOCKET_ERROR) {
        std::cerr << "Failed to read request from client\n";
        closesocket(client_socket);
        return;
    }

    buffer[bytes_received] = '\0'; // Null-terminate the buffer to treat it as a string

    std::string request(buffer);
    std::string response;
    
    // Find the end of the headers and the start of the body
    size_t header_end = request.find("\r\n\r\n");
    std::string headers = request.substr(0, header_end);
    std::string body = request.substr(header_end + 4);

    // Parse the request line
    std::string request_line = request.substr(0, request.find("\r\n"));
    std::string method = request_line.substr(0, request_line.find(' '));
    std::string path = request_line.substr(request_line.find(' ') + 1, request_line.find(' ', request_line.find(' ') + 1) - request_line.find(' ') - 1);

    if (method == "POST" && path.find("/files/") == 0) {
        std::string filename = path.substr(7); // Extract filename from path

        // Extract Content-Length header
        size_t content_length_pos = headers.find("Content-Length: ");
        int content_length = 0;
        if (content_length_pos != std::string::npos) {
            content_length = std::stoi(headers.substr(content_length_pos + 16, headers.find("\r\n", content_length_pos) - content_length_pos - 16));
        }

        // Check if the body length matches the Content-Length header
        if (body.length() != static_cast<size_t>(content_length)) {
            response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        } else {
            std::string file_path = directory + filename;
            std::ofstream file(file_path, std::ios::binary);

            if (file.is_open()) {
                file.write(body.c_str(), body.size());
                file.close();
                response = "HTTP/1.1 201 Created\r\n\r\n";
            } else {
                response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
            }
        }
    } else if (method == "GET" && path.find("/files/") == 0) {
        std::string filename = path.substr(7); // Extract filename from path

        std::string file_path = directory + filename;
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        
        if (file.is_open()) {
            std::streamsize file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::string file_content(file_size, '\0');
            
            if (file.read(&file_content[0], file_size)) {
                response = "HTTP/1.1 200 OK\r\n";
                response += "Content-Type: application/octet-stream\r\n";
                response += "Content-Length: " + std::to_string(file_size) + "\r\n";
                response += "\r\n";
                response += file_content;
            } else {
                response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
            }
            file.close();
        } else {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
    } else {
        response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    }

    // Send the HTTP response to the client
    send(client_socket, response.c_str(), response.length(), 0);
    std::cout << "Response sent to client\n";

    closesocket(client_socket);
}

int main(int argc, char **argv) {
    if (argc != 3 || std::strcmp(argv[1], "--directory") != 0) {
        std::cerr << "Usage: " << argv[0] << " --directory <path>\n";
        return 1;
    }
    
    directory = argv[2];
    // Ensure directory path ends with a '/'
    if (directory.back() != '/') {
        directory += '/';
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_socket, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind to port 4221\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 4221...\n";

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }

        // Handle client request
        handle_client(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
