#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <poll.h>

#include "Client.hpp"
#include "Channel.hpp"

class Server {
private:
    int _port;
    std::string _password;
    int _serverSocket;
    std::vector<struct pollfd> _pollFds;
    std::map<int, Client> _clients;
    std::map<std::string, Channel> _channels;

    void    setupServerSocket();
    void    handleNewConnection();
    void    handleClientRead(int fd);
    void    handleClientWrite(int fd);
    void    removeClient(int fd);
    void    setPollOut(int fd, bool enable);

    // Command processing
    void    processCommand(Client &client, const std::string &command);
    
    // Commands
    void    cmdPass(Client &client, std::istringstream &iss);
    void    cmdNick(Client &client, std::istringstream &iss);
    void    cmdUser(Client &client, std::istringstream &iss);
    void    cmdJoin(Client &client, std::istringstream &iss);
    void    cmdPrivmsg(Client &client, std::istringstream &iss, bool isNotice);
    void    cmdKick(Client &client, std::istringstream &iss);
    void    cmdInvite(Client &client, std::istringstream &iss);
    void    cmdTopic(Client &client, std::istringstream &iss);
    void    cmdMode(Client &client, std::istringstream &iss);

    // Helpers
    void    sendToClient(int fd, const std::string &msg);
    void    broadcastToChannel(const std::string &channelName, const std::string &msg, int senderFd);
    Client* getClientByNick(const std::string &nick);

public:
    Server(int port, const std::string &password);
    ~Server();

    void    run();

    // Exceptions
    class SocketException : public std::exception {
        public: const char* what() const throw() { return "Socket creation failed"; }
    };
    class BindException : public std::exception {
        public: const char* what() const throw() { return "Bind failed"; }
    };
    class ListenException : public std::exception {
        public: const char* what() const throw() { return "Listen failed"; }
    };
};

#endif
