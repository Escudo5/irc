#include "Server.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::cerr << "Error: Invalid port number." << std::endl;
        return 1;
    }
    std::string password = argv[2];

    if (password.empty()) {
        std::cerr << "Error: Password cannot be empty." << std::endl;
        return 1;
    }

    try {
        Server server(port, password);
        server.run();
    } catch (std::exception &e) {
        std::cerr << "Server Initialization Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
