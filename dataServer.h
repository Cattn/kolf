#ifndef DATASERVER_H
#define DATASERVER_H

#include <iostream>
#include <string>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif
#include <thread>
#include <sstream>
#include <json.hpp>

void sendJsonToServer(const char* json_data, const char* path);
void sendTestDataToServer();
void updateData(const char* value, const std::string& dataTypeInfo, const std::string& path);
void resetData();
void updateDoubleData(const double& value, const std::string& dataTypeInfo, const std::string& path);

#endif