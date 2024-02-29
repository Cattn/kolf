#include "dataServer.h"

const int PORT = 3010;
const char* SERVER_IP = "127.0.0.1";


class ShotClass {
        public:
            std::string name;
            double x;
            double y;
            double angleStart;
            double angleEnd;
            float startDistance;
            float endDistance;
            float magnitude;
            int aimTime;
            time_t shotTime;
            std::string path = "shot";
            void clear() {
                name = "";
                x = 0;
                y = 0;
                angleStart = 0;
                angleEnd = 0;
                startDistance = 0;
                endDistance = 0;
                magnitude = 0;
                aimTime = 0;
                shotTime = 0;
            };
    };

 class HoleClass {
        public:
            std::string name;
            int shots;
            int score;
            int par;
            int aimTime;
            int ranking;
            std::string path = "hole";
            void clear() {
                name = "";
                shots = 0;
                score = 0;
                par = 0;
                aimTime = 0;
                ranking = 0;
            };
};

ShotClass shot;
HoleClass hole;

void updateData(const char* value, const std::string& dataTypeInfo, const std::string& path) {
    if (path == "shot") {
        if (dataTypeInfo == "name") {
            shot.name = value;
        } else if (dataTypeInfo == "x") {
            shot.x = std::stod(value);
        } else if (dataTypeInfo == "y") {
            shot.y = std::stod(value);
        } else if (dataTypeInfo == "angleStart") {
            shot.angleStart = std::stod(value);
        } else if (dataTypeInfo == "angleEnd") {
            shot.angleEnd = std::stod(value);
        } else if (dataTypeInfo == "startDistance") {
            shot.startDistance = std::stof(value);
        } else if (dataTypeInfo == "endDistance") {
            shot.endDistance = std::stof(value);
        } else if (dataTypeInfo == "magnitude") {
            shot.magnitude = std::stof(value);
        } else if (dataTypeInfo == "aimTime") {
            shot.aimTime = std::stoi(value);
        } else if (dataTypeInfo == "shotTime") {
            shot.shotTime = static_cast<time_t>(std::stol(value));
        }
    } else if (path == "hole") {
        if (dataTypeInfo == "name") {
            hole.name = value;
        } else if (dataTypeInfo == "shots") {
            hole.shots = std::stoi(value);
        } else if (dataTypeInfo == "score") {
            hole.score = std::stoi(value);
        } else if (dataTypeInfo == "par") {
            hole.par = std::stoi(value);
        } else if (dataTypeInfo == "aimTime") {
            hole.aimTime = std::stoi(value);
        } else if (dataTypeInfo == "ranking") {
            hole.ranking = std::stoi(value);
        }
    }
}

std::string escapeJsonString(const std::string& s) {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            // Add other cases as necessary
            default: o << c;
        }
    }
    return o.str();
}


void sendTestDataToServer() {
    nlohmann::json j;
    j["/turn"]["name"] = shot.name;
    j["/shot"]["name"] = shot.name;
    j["/shot"]["x"] = shot.x;
    j["/shot"]["y"] = shot.y;
    j["/shot"]["aim-start-angle"] = shot.angleStart;
    j["/shot"]["aim-end-angle"] = shot.angleEnd;
    j["/shot"]["aim-start-distance"] = shot.startDistance;
    j["/shot"]["aim-end-distance"] = shot.endDistance;
    j["/shot"]["force"] = shot.magnitude;
    j["/shot"]["aim-time"] = shot.aimTime;
    j["/shot"]["timestamp"] = shot.shotTime;
    
    nlohmann::json holeJson;
    holeJson["name"] = hole.name;
    holeJson["shots"] = hole.shots;
    holeJson["score"] = hole.score;
    holeJson["par"] = hole.par;
    holeJson["time"] = hole.aimTime;
    holeJson["place"] = hole.ranking;
    
    j["/hole"].push_back(holeJson);

    std::string jsonStr = j.dump();
    sendJsonToServer(jsonStr.c_str(), "dev");
}

void resetData() {
    hole.clear();
    shot.clear();
}

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


    if (connect(client_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == SO_ERROR) {
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
