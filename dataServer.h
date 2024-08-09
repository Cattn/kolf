#ifndef DATASERVER_H
#define DATASERVER_H

#include <vector>
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

void sendJsonToServer(const char *json_data, const char *path);
void sendShotDataToServer();
void sendTurnDataToServer();
void updateData(const std::string& value, const std::string &dataTypeInfo, const std::string &path);
void resetData();
void updateDoubleData(const double &value, const std::string &dataTypeInfo, const std::string &path);
class HoleClass
{
public:
    std::string name;
    std::string holeName;
    int shots;
    int score;
    int par;
    int aimTime;
    int ranking;
    std::string path = "hole";

    void clear()
    {
        name = "";
        shots = 0;
        score = 0;
        par = 0;
        aimTime = 0;
        ranking = 0;
    };
};

void sendHoleDataToServer(HoleClass& hole);
#endif