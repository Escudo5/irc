#include "Channel.hpp"
#include <algorithm>

Channel::Channel(const std::string &name) : _name(name), _inviteOnly(false), _topicRestricted(true), _password(""), _userLimit(0) {}

Channel::~Channel() {}

std::string Channel::getName() const { return _name; }

std::string Channel::getTopic() const { return _topic; }

void Channel::setTopic(const std::string &topic) { _topic = topic; }

void Channel::addClient(int fd) {
    if (!hasClient(fd)) {
        _clientsFds.push_back(fd);
    }
}

void Channel::removeClient(int fd) {
    _clientsFds.erase(std::remove(_clientsFds.begin(), _clientsFds.end(), fd), _clientsFds.end());
    removeOperator(fd);
}

bool Channel::hasClient(int fd) const {
    return std::find(_clientsFds.begin(), _clientsFds.end(), fd) != _clientsFds.end();
}

void Channel::addOperator(int fd) {
    if (!isOperator(fd)) {
        _operatorsFds.push_back(fd);
    }
}

void Channel::removeOperator(int fd) {
    _operatorsFds.erase(std::remove(_operatorsFds.begin(), _operatorsFds.end(), fd), _operatorsFds.end());
}

bool Channel::isOperator(int fd) const {
    return std::find(_operatorsFds.begin(), _operatorsFds.end(), fd) != _operatorsFds.end();
}

bool Channel::isInviteOnly() const { return _inviteOnly; }
void Channel::setInviteOnly(bool inviteOnly) { _inviteOnly = inviteOnly; }

bool Channel::isTopicRestricted() const { return _topicRestricted; }
void Channel::setTopicRestricted(bool topicRestricted) { _topicRestricted = topicRestricted; }

std::string Channel::getPassword() const { return _password; }
void Channel::setPassword(const std::string &password) { _password = password; }

size_t Channel::getUserLimit() const { return _userLimit; }
void Channel::setUserLimit(size_t limit) { _userLimit = limit; }

void Channel::inviteClient(int fd) {
    if (!isInvited(fd)) {
        _invitedFds.push_back(fd);
    }
}

bool Channel::isInvited(int fd) const {
    return std::find(_invitedFds.begin(), _invitedFds.end(), fd) != _invitedFds.end();
}

bool Channel::isEmpty() const {
    return _clientsFds.empty();
}

const std::vector<int>& Channel::getClients() const {
    return _clientsFds;
}
