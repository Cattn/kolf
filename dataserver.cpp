#include "dataServer.h"

const int PORT = 3010;
const char* SERVER_IP = "127.0.0.1";

void sendJsonToServer(const char* json_data, const char* path) {
	// if windows
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return;
    }
#endif


    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Error creating socket\n";
#ifdef _WIN32
        std::cerr << "Error code: " << WSAGetLastError() << std::endl;
        WSACleanup();
#endif
        return;
    }


    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr);


    if (connect(client_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Error connecting to the server\n";
#ifdef _WIN32
        std::cerr << "Error code: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
#else
        closesocket(client_socket);
#endif
        return;
    }

    
    std::string http_request = "POST /" + std::string(path) + " HTTP/1.1\r\n"
                               "Host: " + std::string(SERVER_IP) + "\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(strlen(json_data)) + "\r\n"
                               "\r\n" + std::string(json_data);


    send(client_socket, http_request.c_str(), http_request.size(), 0);


    closesocket(client_socket);


#ifdef _WIN32
    WSACleanup();
#endif
}
