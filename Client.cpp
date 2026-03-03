#include "Client.hpp"

Client::Client() : _fd(-1), _hasPassed(false), _isRegistered(false) {}

Client::Client(int fd) : _fd(fd), _hasPassed(false), _isRegistered(false) {}

Client::~Client() {}

int Client::getFd() const { return _fd; }

std::string Client::getNickname() const { return _nickname; }

std::string Client::getUsername() const { return _username; }

bool Client::hasPassed() const { return _hasPassed; }

bool Client::isRegistered() const { return _isRegistered; }

void Client::setNickname(const std::string &nick) {
    _nickname = nick;
    checkRegistration();
}

void Client::setUsername(const std::string &user) {
    _username = user;
    checkRegistration();
}

void Client::setPassed(bool passed) {
    _hasPassed = passed;
}

void Client::checkRegistration() {
    if (_hasPassed && !_nickname.empty() && !_username.empty()) {
        _isRegistered = true;
    }
}

void Client::appendInBuffer(const std::string &data) {
    _inBuffer += data;
}

bool Client::extractLine(std::string &line) {
    size_t pos = _inBuffer.find("\r\n");
    if (pos == std::string::npos) {
        pos = _inBuffer.find('\n');
    }

    if (pos != std::string::npos) {
        line = _inBuffer.substr(0, pos);
        size_t skip = (_inBuffer[pos] == '\r') ? 2 : 1;
        _inBuffer.erase(0, pos + skip);
        return true;
    }
    return false;
}

void Client::appendOutBuffer(const std::string &data) {
    _outBuffer += data;
}

const std::string& Client::getOutBuffer() const {
    return _outBuffer;
}

void Client::eraseOutBuffer(size_t n) {
    if (n <= _outBuffer.length()) {
        _outBuffer.erase(0, n);
    }
}
