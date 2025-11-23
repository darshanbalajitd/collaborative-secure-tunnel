#ifndef LISTENER_HPP
#define LISTENER_HPP

#include <string>
#include <cstdint>

class Listener {
public:
    Listener(int port);
    ~Listener();

    bool start();
    intptr_t accept_connection();

private:
    int port_;
    intptr_t listen_fd_ = -1;
};

#endif // LISTENER_HPP