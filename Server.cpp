#include "Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sstream>
#include <cstdlib>

Server::Server(int port, const std::string &password) : _port(port), _password(password) {
    setupServerSocket();
}

Server::~Server() {
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        close(_pollFds[i].fd);
    }
}


// configurar socket principal.
//AF_INET: ipv4
//SOCK_STREAM:tcp
//0: el sistema elige el protocolo.

//setsockopt: configura opciones del socket.
//SOL_SOCKET: nivel de la opcion. 
//SO_REUSEADDR: permite reutilizar la direccion.
void Server::setupServerSocket() {
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0) throw SocketException();

    int opt = 1;
    setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (fcntl(_serverSocket, F_SETFL, O_NONBLOCK) < 0) {
        close(_serverSocket);
        throw SocketException();
    }

    struct sockaddr_in address; //describe una direccion de red ipv4
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(_port); //convertir a big-endian para q entienda la red

    if (bind(_serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(_serverSocket);
        throw BindException();
    }

    if (listen(_serverSocket, SOMAXCONN) < 0) {
        close(_serverSocket); // lo cerramos porq no se usa para env o rec, solamente escucha.
        throw ListenException();
    }

    struct pollfd srvPollFd;
    srvPollFd.fd = _serverSocket;
    srvPollFd.events = POLLIN; //eventos q nos interesan
    srvPollFd.revents = 0; //eventos que ya han pasado
    _pollFds.push_back(srvPollFd);
}

void Server::run() {
    std::cout << "IRC Server started on port " << _port << std::endl;
    
    while (true) {
        if (poll(&_pollFds[0], _pollFds.size(), -1) < 0) {
            std::cerr << "Poll error." << std::endl;
            break;
        }

        for (size_t i = 0; i < _pollFds.size(); ++i) {
            if (_pollFds[i].revents & POLLIN) {
                if (_pollFds[i].fd == _serverSocket) {
                    handleNewConnection();
                } else {
                    handleClientRead(_pollFds[i].fd);
                }
            }
            if (_pollFds[i].revents & POLLOUT) {
                handleClientWrite(_pollFds[i].fd);
            }
            if (_pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (_pollFds[i].fd != _serverSocket) {
                    removeClient(_pollFds[i].fd);
                }
            }
        }
    }
}

void Server::handleNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    int clientFd = accept(_serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
    if (clientFd >= 0) {
        if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
            close(clientFd);
            return;
        }

        struct pollfd pfd;
        pfd.fd = clientFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _pollFds.push_back(pfd);
        
        _clients.insert(std::make_pair(clientFd, Client(clientFd)));
        std::cout << "New connection: FD " << clientFd << std::endl;
    }
}

void Server::handleClientRead(int fd) {
    char buffer[1024];
    int bytesRead = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0) {
        removeClient(fd);
    } else {
        buffer[bytesRead] = '\0';
        if (_clients.find(fd) != _clients.end()) {
            Client &client = _clients[fd];
            client.appendInBuffer(buffer);
            std::string line;
            while (client.extractLine(line)) {
                processCommand(client, line);
            }
        }
    }
}

void Server::handleClientWrite(int fd) {
    if (_clients.find(fd) != _clients.end()) {
        Client &client = _clients[fd];
        const std::string &out = client.getOutBuffer();
        if (!out.empty()) {
            int bytesSent = send(fd, out.c_str(), out.length(), 0);
            if (bytesSent > 0) {
                client.eraseOutBuffer(bytesSent);
            } else if (bytesSent < 0) {
                // Ignore EAGAIN, handle actual errors if needed
            }
        }
        
        if (client.getOutBuffer().empty()) {
            setPollOut(fd, false);
        }
    }
}

void Server::setPollOut(int fd, bool enable) {
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        if (_pollFds[i].fd == fd) {
            if (enable) _pollFds[i].events |= POLLOUT;
            else _pollFds[i].events &= ~POLLOUT;
            break;
        }
    }
}

void Server::sendToClient(int fd, const std::string &msg) {
    if (_clients.find(fd) != _clients.end()) {
        Client &client = _clients[fd];
        client.appendOutBuffer(msg + "\r\n");
        setPollOut(fd, true);
    }
}

void Server::broadcastToChannel(const std::string &channelName, const std::string &msg, int senderFd) {
    if (_channels.find(channelName) != _channels.end()) {
        const std::vector<int> &clients = _channels[channelName].getClients();
        for (size_t i = 0; i < clients.size(); ++i) {
            if (clients[i] != senderFd) {
                sendToClient(clients[i], msg);
            }
        }
    }
}

Client* Server::getClientByNick(const std::string &nick) {
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second.getNickname() == nick) {
            return &(it->second);
        }
    }
    return NULL;
}

void Server::removeClient(int fd) {
    std::cout << "Client disconnected: FD " << fd << std::endl;
    for (std::vector<struct pollfd>::iterator it = _pollFds.begin(); it != _pollFds.end(); ++it) {
        if (it->fd == fd) {
            _pollFds.erase(it);
            break;
        }
    }
    
    // Remove from all channels
    for (std::map<std::string, Channel>::iterator it = _channels.begin(); it != _channels.end(); /* dynamic */) {
        it->second.removeClient(fd);
        if (it->second.isEmpty()) {
            std::map<std::string, Channel>::iterator to_erase = it;
            ++it;
            _channels.erase(to_erase);
        } else {
            ++it;
        }
    }

    _clients.erase(fd);
    close(fd);
}

void Server::processCommand(Client &client, const std::string &command) {
    if (command.empty()) return;

    std::cout << "Received from FD " << client.getFd() << ": " << command << std::endl;
    
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    if (cmd == "PASS") {
        cmdPass(client, iss);
    } else if (cmd == "NICK") {
        cmdNick(client, iss);
    } else if (cmd == "USER") {
        cmdUser(client, iss);
    } else if (cmd == "QUIT") {
        removeClient(client.getFd());
    } else {
        if (!client.isRegistered()) {
            sendToClient(client.getFd(), ":server 451 " + client.getNickname() + " :You have not registered");
            return;
        }

        if (cmd == "JOIN") {
            cmdJoin(client, iss);
        } else if (cmd == "PRIVMSG") {
            cmdPrivmsg(client, iss, false);
        } else if (cmd == "NOTICE") {
            cmdPrivmsg(client, iss, true);
        } else if (cmd == "KICK") {
            cmdKick(client, iss);
        } else if (cmd == "INVITE") {
            cmdInvite(client, iss);
        } else if (cmd == "TOPIC") {
            cmdTopic(client, iss);
        } else if (cmd == "MODE") {
            cmdMode(client, iss);
        } else {
            sendToClient(client.getFd(), ":server 421 " + client.getNickname() + " " + cmd + " :Unknown command");
        }
    }
}

void Server::cmdPass(Client &client, std::istringstream &iss) {
    std::string pass;
    iss >> pass;
    if (pass == _password) {
        client.setPassed(true);
    } else {
        sendToClient(client.getFd(), ":server 464 :Password incorrect");
    }
}

void Server::cmdNick(Client &client, std::istringstream &iss) {
    std::string nick;
    iss >> nick;
    if (nick.empty()) {
        sendToClient(client.getFd(), ":server 431 :No nickname given");
        return;
    }
    if (getClientByNick(nick) != NULL) {
        sendToClient(client.getFd(), ":server 433 * " + nick + " :Nickname is already in use");
        return;
    }
    
    std::string oldNick = client.getNickname();
    client.setNickname(nick);
    
    if (client.isRegistered() && !oldNick.empty()) {
        std::string msg = ":" + oldNick + " NICK :" + nick;
        sendToClient(client.getFd(), msg);
    } else if (client.isRegistered() && oldNick.empty()) {
        sendToClient(client.getFd(), ":server 001 " + nick + " :Welcome to the ft_irc network");
    }
}

void Server::cmdUser(Client &client, std::istringstream &iss) {
    if (client.isRegistered()) {
        sendToClient(client.getFd(), ":server 462 :You may not reregister");
        return;
    }
    std::string user;
    iss >> user;
    if (!user.empty()) {
        client.setUsername(user);
        if (client.isRegistered()) {
            sendToClient(client.getFd(), ":server 001 " + client.getNickname() + " :Welcome to the ft_irc network");
        }
    }
}

void Server::cmdJoin(Client &client, std::istringstream &iss) {
    std::string chanName, key;
    iss >> chanName >> key;
    if (chanName.empty() || chanName[0] != '#') {
        sendToClient(client.getFd(), ":server 461 " + client.getNickname() + " JOIN :Not enough parameters");
        return;
    }
    
    if (_channels.find(chanName) == _channels.end()) {
        _channels[chanName] = Channel(chanName);
        _channels[chanName].addOperator(client.getFd()); // First to join is operator
    } else {
        Channel &ch = _channels[chanName];
        if (ch.isInviteOnly() && !ch.isInvited(client.getFd())) {
            sendToClient(client.getFd(), ":server 473 " + client.getNickname() + " " + chanName + " :Cannot join channel (+i)");
            return;
        }
        if (!ch.getPassword().empty() && ch.getPassword() != key) {
            sendToClient(client.getFd(), ":server 475 " + client.getNickname() + " " + chanName + " :Cannot join channel (+k)");
            return;
        }
        if (ch.getUserLimit() > 0 && ch.getClients().size() >= ch.getUserLimit()) {
            sendToClient(client.getFd(), ":server 471 " + client.getNickname() + " " + chanName + " :Cannot join channel (+l)");
            return;
        }
    }
    
    _channels[chanName].addClient(client.getFd());
    
    std::string joinMsg = ":" + client.getNickname() + " JOIN :" + chanName;
    broadcastToChannel(chanName, joinMsg, -1);
    
    if (!_channels[chanName].getTopic().empty()) {
        sendToClient(client.getFd(), ":server 332 " + client.getNickname() + " " + chanName + " :" + _channels[chanName].getTopic());
    }
}

void Server::cmdPrivmsg(Client &client, std::istringstream &iss, bool isNotice) {
    std::string target, text;
    iss >> target;
    std::getline(iss, text);
    
    if (target.empty() || text.empty()) {
        if (!isNotice) sendToClient(client.getFd(), ":server 411 :No recipient or text given");
        return;
    }
    
    if (text.length() > 0 && text[0] == ' ') text.erase(0, 1);
    if (text.length() > 0 && text[0] == ':') text.erase(0, 1);

    std::string msg = ":" + client.getNickname() + " " + (isNotice ? "NOTICE" : "PRIVMSG") + " " + target + " :" + text;

    if (target[0] == '#') {
        if (_channels.find(target) == _channels.end() || !_channels[target].hasClient(client.getFd())) {
            if (!isNotice) sendToClient(client.getFd(), ":server 404 " + client.getNickname() + " " + target + " :Cannot send to channel");
            return;
        }
        broadcastToChannel(target, msg, client.getFd());
    } else {
        Client *targetClient = getClientByNick(target);
        if (targetClient == NULL) {
            if (!isNotice) sendToClient(client.getFd(), ":server 401 " + client.getNickname() + " " + target + " :No such nick/channel");
            return;
        }
        sendToClient(targetClient->getFd(), msg);
    }
}

void Server::cmdKick(Client &client, std::istringstream &iss) {
    std::string chanName, userNick, reason;
    iss >> chanName >> userNick;
    std::getline(iss, reason);

    if (chanName.empty() || userNick.empty()) {
        sendToClient(client.getFd(), ":server 461 KICK :Not enough parameters");
        return;
    }

    if (_channels.find(chanName) == _channels.end()) {
        sendToClient(client.getFd(), ":server 403 " + chanName + " :No such channel");
        return;
    }

    Channel &ch = _channels[chanName];
    if (!ch.hasClient(client.getFd())) {
        sendToClient(client.getFd(), ":server 442 " + chanName + " :You're not on that channel");
        return;
    }

    if (!ch.isOperator(client.getFd())) {
        sendToClient(client.getFd(), ":server 482 " + chanName + " :You're not channel operator");
        return;
    }

    Client *targetClient = getClientByNick(userNick);
    if (!targetClient || !ch.hasClient(targetClient->getFd())) {
        sendToClient(client.getFd(), ":server 441 " + userNick + " " + chanName + " :They aren't on that channel");
        return;
    }

    if (reason.length() > 0 && reason[0] == ' ') reason.erase(0, 1);
    
    std::string kickMsg = ":" + client.getNickname() + " KICK " + chanName + " " + userNick + (reason.empty() ? "" : " :" + reason);
    broadcastToChannel(chanName, kickMsg, -1);
    ch.removeClient(targetClient->getFd());
}

void Server::cmdInvite(Client &client, std::istringstream &iss) {
    std::string targetNick, chanName;
    iss >> targetNick >> chanName;

    if (targetNick.empty() || chanName.empty()) {
        sendToClient(client.getFd(), ":server 461 " + client.getNickname() + " INVITE :Not enough parameters");
        return;
    }

    if (_channels.find(chanName) == _channels.end()) {
        sendToClient(client.getFd(), ":server 403 " + client.getNickname() + " " + chanName + " :No such channel");
        return;
    }

    Channel &ch = _channels[chanName];
    if (!ch.hasClient(client.getFd())) {
        sendToClient(client.getFd(), ":server 442 " + client.getNickname() + " " + chanName + " :You're not on that channel");
        return;
    }

    if (ch.isInviteOnly() && !ch.isOperator(client.getFd())) {
        sendToClient(client.getFd(), ":server 482 " + client.getNickname() + " " + chanName + " :You're not channel operator");
        return;
    }

    Client *targetClient = getClientByNick(targetNick);
    if (!targetClient) {
        sendToClient(client.getFd(), ":server 401 " + client.getNickname() + " " + targetNick + " :No such nick/channel");
        return;
    }

    if (ch.hasClient(targetClient->getFd())) {
        sendToClient(client.getFd(), ":server 443 " + client.getNickname() + " " + targetNick + " " + chanName + " :is already on channel");
        return;
    }

    ch.inviteClient(targetClient->getFd());
    sendToClient(client.getFd(), ":server 341 " + client.getNickname() + " " + targetNick + " " + chanName);
    sendToClient(targetClient->getFd(), ":" + client.getNickname() + " INVITE " + targetNick + " :" + chanName);
}

void Server::cmdTopic(Client &client, std::istringstream &iss) {
    std::string chanName, topic;
    iss >> chanName;
    std::getline(iss, topic);

    if (chanName.empty()) {
        sendToClient(client.getFd(), ":server 461 " + client.getNickname() + " TOPIC :Not enough parameters");
        return;
    }

    if (_channels.find(chanName) == _channels.end()) {
        sendToClient(client.getFd(), ":server 403 " + client.getNickname() + " " + chanName + " :No such channel");
        return;
    }

    Channel &ch = _channels[chanName];
    if (!ch.hasClient(client.getFd())) {
        sendToClient(client.getFd(), ":server 442 " + client.getNickname() + " " + chanName + " :You're not on that channel");
        return;
    }

    if (topic.empty()) {
        if (ch.getTopic().empty()) {
            sendToClient(client.getFd(), ":server 331 " + client.getNickname() + " " + chanName + " :No topic is set");
        } else {
            sendToClient(client.getFd(), ":server 332 " + client.getNickname() + " " + chanName + " :" + ch.getTopic());
        }
        return;
    }

    if (topic.length() > 0 && topic[0] == ' ') topic.erase(0, 1);
    if (topic.length() > 0 && topic[0] == ':') {
        topic.erase(0, 1);
        if (ch.isTopicRestricted() && !ch.isOperator(client.getFd())) {
            sendToClient(client.getFd(), ":server 482 " + client.getNickname() + " " + chanName + " :You're not channel operator");
            return;
        }
        ch.setTopic(topic);
        broadcastToChannel(chanName, ":" + client.getNickname() + " TOPIC " + chanName + " :" + topic, -1);
    } else if (topic.empty() || topic == ":") {
        if (ch.isTopicRestricted() && !ch.isOperator(client.getFd())) {
            sendToClient(client.getFd(), ":server 482 " + client.getNickname() + " " + chanName + " :You're not channel operator");
            return;
        }
        ch.setTopic("");
        broadcastToChannel(chanName, ":" + client.getNickname() + " TOPIC " + chanName + " :", -1);
    }
}

void Server::cmdMode(Client &client, std::istringstream &iss) {
    std::string target, modeStr;
    iss >> target;
    
    if (target.empty()) {
        sendToClient(client.getFd(), ":server 461 " + client.getNickname() + " MODE :Not enough parameters");
        return;
    }

    if (target[0] != '#') return;

    if (_channels.find(target) == _channels.end()) {
        sendToClient(client.getFd(), ":server 403 " + client.getNickname() + " " + target + " :No such channel");
        return;
    }

    Channel &ch = _channels[target];

    if (!(iss >> modeStr)) {
        std::string modes = "+";
        if (ch.isInviteOnly()) modes += "i";
        if (ch.isTopicRestricted()) modes += "t";
        if (!ch.getPassword().empty()) modes += "k";
        if (ch.getUserLimit() > 0) modes += "l";
        sendToClient(client.getFd(), ":server 324 " + client.getNickname() + " " + target + " " + modes);
        return;
    }

    if (!ch.isOperator(client.getFd())) {
        sendToClient(client.getFd(), ":server 482 " + client.getNickname() + " " + target + " :You're not channel operator");
        return;
    }

    bool adding = true;
    for (size_t i = 0; i < modeStr.length(); ++i) {
        char m = modeStr[i];
        if (m == '+') { adding = true; continue; }
        if (m == '-') { adding = false; continue; }

        if (m == 'i') {
            ch.setInviteOnly(adding);
            broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " " + (adding ? "+i" : "-i"), -1);
        } else if (m == 't') {
            ch.setTopicRestricted(adding);
            broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " " + (adding ? "+t" : "-t"), -1);
        } else if (m == 'k') {
            if (adding) {
                std::string key;
                iss >> key;
                if (!key.empty()) {
                    ch.setPassword(key);
                    broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " +k " + key, -1);
                }
            } else {
                ch.setPassword("");
                broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " -k", -1);
            }
        } else if (m == 'l') {
            if (adding) {
                std::string limitStr;
                iss >> limitStr;
                int limit = std::atoi(limitStr.c_str());
                if (limit > 0) {
                    ch.setUserLimit(limit);
                    broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " +l " + limitStr, -1);
                }
            } else {
                ch.setUserLimit(0);
                broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " -l", -1);
            }
        } else if (m == 'o') {
            std::string userNick;
            iss >> userNick;
            if (!userNick.empty()) {
                Client *targetClient = getClientByNick(userNick);
                if (targetClient && ch.hasClient(targetClient->getFd())) {
                    if (adding) {
                        ch.addOperator(targetClient->getFd());
                        broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " +o " + userNick, -1);
                    } else {
                        ch.removeOperator(targetClient->getFd());
                        broadcastToChannel(target, ":" + client.getNickname() + " MODE " + target + " -o " + userNick, -1);
                    }
                }
            }
        }
    }
}
