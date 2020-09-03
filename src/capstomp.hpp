#pragma once

#include "url.hpp"
#include "btpro/socket.hpp"
#include "btpro/buffer.hpp"
#include "stompconn/stomplay.hpp"
#include "stompconn/frame.hpp"
#include <string_view>
#include <mutex>

namespace cs {

class connection
{
    // connection string
    std::string uri_{};
    // stomp protocol error
    std::string error_{};
    std::mutex mutex_{};
    bool own_lock_{false};

    // connection timeout
    int timeout_{10000};

    btpro::socket socket_{};
    stompconn::stomplay stomplay_{};
public:

    connection();

    ~connection();

    void close() noexcept;

    // lock and connect
    void connect(std::string_view uri);

    template<class F>
    std::size_t send(F frame)
    {
        return frame.write_all(socket_);
    }

    void unlock();

    void set_timeout(int timeout) noexcept;

private:

    void lock();

    bool connected();

    void logon(const url& u, int timeout);

    bool ready_read(int timeout);

    void read_logon(int timeout);

    bool read_stomp();

};

} // namespace cs
