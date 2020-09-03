#include "capstomp.hpp"
#include "btpro/sock_addr.hpp"
#include <poll.h>

namespace cs {

void connection::close() noexcept
{
    socket_.close();
    uri_.clear();
}

connection::connection()
{
    stomplay_.on_logon([&](stompconn::packet){
    });

    stomplay_.on_error([&](stompconn::packet packet){
        error_ = packet.payload().str();
    });
}

connection::~connection()
{
    close();
}

// подключаемся только на локалхост
void connection::connect(std::string_view uri)
{
    // захватыаем соединение
    lock();

    if (uri_ != uri)
        close();

    // проверяем связь и было ли отключение
    if (!connected())
    {
        url u(uri);
        btpro::sock_addr addr(u.addr());

        auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (btpro::code::fail == fd)
            throw std::system_error(btpro::net::error_code(), "socket");

        auto res = ::connect(fd, addr.sa(), addr.size());
        if (btpro::code::fail == res)
            throw std::system_error(btpro::net::error_code(), "connect");

        socket_.attach(fd);

        uri_ = uri;
        // сбрасываем ошибку
        error_.clear();

        logon(u, timeout_);

        if (!error_.empty())
            throw std::runtime_error(error_);
    }
}

bool connection::connected()
{
    // жив ли сокет
    if (!socket_.good())
        return false;

    while (ready_read(0))
    {
        if (!read_stomp())
        {
            socket_.close();
            return false;
        }
    }

    return true;
}

void connection::lock()
{
    mutex_.lock();
    own_lock_ = true;
}

void connection::unlock()
{
    if (own_lock_)
    {
        own_lock_ = false;
        mutex_.unlock();
    }
}

void connection::set_timeout(int timeout) noexcept
{
    timeout_ = timeout;
}

void connection::logon(const url& u, int timeout)
{
    send(stompconn::logon(u.vhost(), u.user(), u.passcode()));
    read_logon(timeout);
}

bool connection::ready_read(int timeout)
{
    auto ev = pollfd{
        socket_.fd(), POLLIN, 0
    };

    auto rc = poll(&ev, 1, timeout);

    if (btpro::code::fail == rc)
        throw std::runtime_error("connection select");

    return 1 == rc;
}

void connection::read_logon(int timeout)
{
    do {
        // ждем события чтения
        if (ready_read(timeout))
        {
            if (!read_stomp())
            {
                socket_.close();

                if (!error_.empty())
                    error_ = stomptalk::sv("read_logon disconnect");
            }
        }
        else
            throw std::runtime_error("read_logon timeout");
        // пока не получили сессию
    } while (stomplay_.session().empty() && error_.empty());
}

bool connection::read_stomp()
{
    // читаем
    char input[2048];
    auto rc = ::recv(socket_.fd(), input, sizeof(input), 0);
    if (btpro::code::fail == rc)
        throw std::runtime_error("read_stomp recv");

    if (rc)
    {
        auto size = static_cast<std::size_t>(rc);
        if (size != stomplay_.parse(input, size))
            throw std::runtime_error("read_stomp parse");

        return true;
    }

    return false;
}

} // namespace capstomp
