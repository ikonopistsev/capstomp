#pragma once

#include "btpro/socket.hpp"
#include "btpro/buffer.hpp"
#include "stompconn/stomplay.hpp"
#include "stompconn/frame.hpp"
#include <string_view>
#include <mutex>

namespace capstomp {

class destination
{
    std::string_view login_;
    std::string_view passcode_;
    std::string_view vhost_;
    btpro::ipv4::addr addr_;

public:
    destination(std::string_view login, std::string_view passcode,
                std::string_view vhost, const btpro::ipv4::addr& addr);

    const btpro::ip::addr& addr() const noexcept
    {
        return addr_;
    }

    std::string_view login() const noexcept
    {
        return login_;
    }

    std::string_view passcode() const noexcept
    {
        return passcode_;
    }

    std::string_view vhost() const noexcept
    {
        return vhost_;
    }

    std::size_t endpoint() const noexcept;
};

class connection
{
    btpro::buffer input_{};
    btpro::socket socket_{};
    stompconn::stomplay stomplay_{};
    std::string error_{};
    std::mutex mutex_{};
    bool own_lock_{false};
    std::size_t endpoint_{};
    int timeout_{10000};
public:

    connection();

    ~connection();

    void close() noexcept;

    // lock and connect
    void connect(const destination& dest);

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

    void logon(const destination& dest, int timeout);

    bool ready_read(int timeout);

    void read_logon(int timeout);

    bool read_stomp();

};

} // namespace capstomp
