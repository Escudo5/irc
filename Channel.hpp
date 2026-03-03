#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>

class Channel {
private:
    std::string _name;
    std::string _topic;
    std::vector<int> _clientsFds;
    std::vector<int> _operatorsFds;

    bool _inviteOnly;
    bool _topicRestricted;
    std::string _password;
    size_t _userLimit;
    std::vector<int> _invitedFds;

public:
    Channel() {}
    Channel(const std::string &name);
    ~Channel();

    std::string getName() const;
    
    std::string getTopic() const;
    void setTopic(const std::string &topic);

    void addClient(int fd);
    void removeClient(int fd);
    bool hasClient(int fd) const;
    
    void addOperator(int fd);
    void removeOperator(int fd);
    bool isOperator(int fd) const;

    bool isInviteOnly() const;
    void setInviteOnly(bool inviteOnly);

    bool isTopicRestricted() const;
    void setTopicRestricted(bool topicRestricted);

    std::string getPassword() const;
    void setPassword(const std::string &password);

    size_t getUserLimit() const;
    void setUserLimit(size_t limit);

    void inviteClient(int fd);
    bool isInvited(int fd) const;
    
    bool isEmpty() const;
    
    const std::vector<int>& getClients() const;
};

#endif
