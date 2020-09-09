#include "connection.hpp"
#include "journal.hpp"
#include "pool.hpp"

#include "btpro/sock_addr.hpp"

#include <poll.h>

using namespace std::literals;

namespace capst {

void connection::close() noexcept
{
#ifdef CAPSTOMP_TRACE_LOG
    if (socket_.good())
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: close socket=";
            text += std::to_string(socket_.fd());
            return text;
        });
#endif

    uri_.clear();
    socket_.close();
    destination_.clear();
}

connection::connection(pool& pool)
    : pool_(pool)
{
    stomplay_.on_logon([&](stompconn::packet logon){
        if (!logon)
        {
            error_ = logon.payload().str();
#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "connection: transaction_id="sv;
                text += transaction_id();
                text += " error: "sv;
                text += error_;
                return text;
            });
#endif
        }
    });

    stomplay_.on_error([&](stompconn::packet packet){
        error_ = packet.payload().str();
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction_id="sv;
            text += transaction_id();
            text += " error: "sv;
            text += error_;
            return text;
        });
#endif
    });
}

connection::~connection()
{
    close();
}

// подключаемся только на локалхост
void connection::connect(std::string_view uri)
{
    if (uri_ != uri)
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction_id="sv;
            text += transaction_id();
            text += " close by changed uri to "sv;
            text += uri;
            return text;
        });
#endif

        close();
    }

    // проверяем связь и было ли отключение
    if (!connected())
    {
        capst::uri u(uri);
        btpro::sock_addr addr(u.addr());

        auto fd = ::socket(addr.family(), SOCK_STREAM, 0);
        if (btpro::code::fail == fd)
            throw std::system_error(btpro::net::error_code(), "socket");

        auto res = ::connect(fd, addr.sa(), addr.size());
        if (btpro::code::fail == res)
            throw std::system_error(btpro::net::error_code(), "connect");

        socket_.attach(fd);

        uri_ = uri;
        destination_ = u.fragment();
        // сбрасываем ошибку
        error_.clear();

        logon(u, timeout_);

        if (!error_.empty())
            throw std::runtime_error(error_);
    }

    // начинаем транзакцию
    begin();

    read_receipt(timeout_);

    if (!error_.empty())
        throw std::runtime_error(error_);
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

void connection::set(int timeout) noexcept
{
    timeout_ = timeout;
}

void connection::set(connection_id_type self) noexcept
{
    self_ = self;
}

void connection::set(transaction_id_type id) noexcept
{
    transaction_ = id;
    transaction_id_ = id->id();
}

void connection::logon(const uri& u, int timeout)
{
    auto path = u.path();

    if (path != "/"sv)
        path = u.rpath();

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction_id="sv;
            text += transaction_id();
            text += " logon with user="sv;
            text += u.user();
            text += " to vhost="sv;
            text += path;
            return text;
        });
#endif

    send(stompconn::logon(path, u.user(), u.passcode()));

    read_logon(timeout);
}

void connection::begin()
{
    stompconn::begin frame(transaction_id_);
    frame.push(stomptalk::header::time_since_epoch());
    auto receipt_id = create_receipt_id(transaction_id_);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction_id="sv;
            text += transaction_id();
            text += " begin: receipt_id="sv;
            text += receipt_id;
            return text;
        });
#endif

    send(std::move(frame), receipt_id, [&](stompconn::packet receipt){
        if (receipt)
        {
#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cout([&, receipt_id]{
                std::string text;
                text.reserve(64);
                text += "begin ok: transaction_id="sv;
                text += transaction_id_;
                text += " receipt_id="sv;
                text += receipt_id;
                return text;
            });
#endif //
        }
        else
        {
#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cerr([&, receipt_id]{
                std::string text;
                text.reserve(64);
                text += "begin error: transaction_id="sv;
                text += transaction_id_;
                text += " receipt_id="sv;
                text += receipt_id;
                return text;
            });
#endif
        }
    });
}

std::size_t connection::commit(transaction_store_type transaction_store)
{
    for (auto& transaction : transaction_store)
    {
        auto transaction_id = transaction.id();
        auto connection_id = transaction.connection();
        if (connection_id->connected())
        {
            stompconn::commit frame(transaction_id);
            frame.push(stomptalk::header::time_since_epoch());
            auto receipt_id = connection_id->create_receipt_id(transaction_id);

#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "connection: transaction_id="sv;
                text += transaction_id;
                text += " send commit: receipt_id="sv;
                text += receipt_id;
                return text;
            });
#endif

            connection_id->send(std::move(frame), receipt_id,
                [&](stompconn::packet receipt){
                    if (receipt)
                    {
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.cout([&, receipt_id]{
                            std::string text;
                            text.reserve(64);
                            text += "commit ok: transaction_id="sv;
                            text += transaction_id;
                            text += " receipt_id="sv;
                            text += receipt_id;
                            return text;
                        });
#endif
                    }
                    else
                    {
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.cerr([&, receipt_id]{
                            std::string text;
                            text.reserve(64);
                            text += "error commit: transaction_id=";
                            text += transaction_id;
                            text += " receipt_id="sv;
                            text += receipt_id;
                            return text;
                        });
#endif
                    }
            });

            connection_id->read_receipt(timeout_);

            if (connection_id != self_)
            {
#ifdef CAPSTOMP_TRACE_LOG
                capst_journal.cout([&]{
                    std::string text;
                    text.reserve(64);
                    text += "connection: transaction_id="sv;
                    text += transaction_id;
                    text += " release deffered"sv;
                    return text;
                });
#endif
                pool_.release(connection_id);
            }
        }
        else
        {
#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cerr([&]{
                std::string text;
                text.reserve(64);
                text += "error commit: "sv;
                text += transaction_id;
                text += " - connecton lost"sv;
                return text;
            });
#endif
        }
    }

    return transaction_store.size();
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
    auto& session = stomplay_.session();

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
    } while (session.empty() && error_.empty());

#ifdef CAPSTOMP_TRACE_LOG
    if (!session.empty())
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction_id="sv;
            text += transaction_id();
            text += " session="sv;
            text += session;
            return text;
        });
    }
#endif
}

void connection::read_receipt(int timeout)
{
    wait_receipt_ = true;

    do {
        // ждем события чтения
        if (ready_read(timeout))
        {
            if (!read_stomp())
            {
                socket_.close();

                if (!error_.empty())
                    error_ = stomptalk::sv("read_receipt disconnect");
            }
        }
        else
            throw std::runtime_error("read_receipt timeout");
        // пока не получили сессию
    } while (wait_receipt_ && error_.empty());
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

void connection::commit()
{
    if (commit(pool_.get_uncommited(transaction_)))
    {
        // если что-то коммитили
        // то релизим и себя
        release();
    }
}

void connection::release()
{
#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.cout([&]{
        std::string text;
        text.reserve(64);
        text += "connection: transaction_id="sv;
        text += transaction_id_;
        text += " release"sv;
        return text;
    });
#endif

    pool_.release(self_);
}

std::string connection::create_receipt_id(std::string_view transaction_id)
{
    std::string rc;
    rc.reserve(64);
    rc = 'R';
    rc += std::to_string(++receipt_id_);
    rc += '#';
    rc += std::to_string(socket_.fd());
    rc += 'T';
    rc += transaction_id;
    return rc;
}

} // namespace capst
