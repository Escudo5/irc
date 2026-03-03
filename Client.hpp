#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>

class Client {
private:
    int         _fd;
    std::string _nickname;
    std::string _username;
    bool        _hasPassed;
    bool        _isRegistered;
    std::string _inBuffer;
    std::string _outBuffer;

public:
    Client();
    Client(int fd);
    ~Client();
    
    // Getters
    int         getFd() const;
    std::string getNickname() const;
    std::string getUsername() const;
    bool        hasPassed() const;
    bool        isRegistered() const;
    
    // Setters
    void        setNickname(const std::string &nick);
    void        setUsername(const std::string &user);
    void        setPassed(bool passed);
    void        checkRegistration();
    
    // Buffers Operations
    void        appendInBuffer(const std::string &data);
    bool        extractLine(std::string &line);
    
    void        appendOutBuffer(const std::string &data);
    const std::string& getOutBuffer() const;
    void        eraseOutBuffer(size_t n);
};

#endif
