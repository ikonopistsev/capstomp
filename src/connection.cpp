#include "connection.hpp"
#include "journal.hpp"
#include "pool.hpp"
#include "conf.hpp"

#include "btpro/sock_addr.hpp"

#include <poll.h>
#include <event2/keyvalq_struct.h>

using namespace std::literals;

namespace capst {

constexpr auto stomp_def = 61613;

#ifdef CAPSTOMP_STATE_DEBUG
#define CAPSTOMP_STATE(x) set_state(x)
#else
#define CAPSTOMP_STATE(x) {}
#endif // CAPSTOMP_STATE_DEBUG

connection::connection(pool& pool)
    : pool_(pool)
{
    stomplay_.on_logon([&](stompconn::packet logon){
        receipt_received_ = true;
        if (!logon)
        {
            auto error = logon.payload().str();
            std::replace(error.begin(), error.end(), '\n', ' ');
            error_ = error;
            capst_journal.cerr([&]{
                std::string text;
                text.reserve(64);
                text += "connection error: destination="sv;
                text += destination_;
                text += ' ';
                text += error_;
                return text;
            });
        }
    });

    stomplay_.on_error([&](stompconn::packet packet){
        receipt_received_ = true;
        auto error = packet.dump();
        std::replace(error.begin(), error.end(), '\n', ' ');
        error_ = error;
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "connection error: destination="sv;
            text += destination_;
            text += ' ';
            text += error_;
            return text;
        });
    });
}

connection::~connection()
{
    close();
}

void connection::close() noexcept
{
    if (socket_.good())
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: close socket="sv;
            text += std::to_string(socket_.fd());
#ifdef CAPSTOMP_STATE_DEBUG
            text += ", state="sv;
            text += std::to_string(state());
#endif
            return text;
        });
    }

    socket_.close();
    destination_.clear();
    passhash_ = std::size_t();
    request_count_ = std::size_t();
    error_.clear();
}

void connection::init(const settings& conf) noexcept
{
    transaction_id_.clear();
    error_.clear();
    conf_ = conf;
}

int socket_poll(btpro::socket socket, short int events, int timeout)
{
    auto ev = pollfd{
        socket.fd(), events, {}
    };

    auto rc = poll(&ev, 1, timeout);
    if (btpro::code::fail == rc)
        throw std::system_error(btpro::net::error_code(), "poll");

    capst_journal.trace([socket, events, timeout, revents = ev.revents]{
        std::string text;
        text.reserve(64);
        text += "connection: socket="sv;
        text += std::to_string(socket.fd());
        if (events & POLLOUT)
        {
            text += " POLLOUT="sv;
            text += std::to_string(revents & POLLOUT);
        }
        if (events & POLLIN)
        {
            text += " POLLIN="sv;
            text += std::to_string(revents & POLLIN);
        }
        text += " timeout="sv;
        text += std::to_string(timeout);
        return text;
    });

    return ev.revents;
}

bool socket_ready_write(btpro::socket socket, int timeout)
{
    return (POLLOUT & socket_poll(socket, POLLOUT, timeout)) > 0;
}

void connect_sync(btpro::socket socket, btpro::ip::addr addr, int timeout)
{
    auto rc = ::connect(socket.fd(), addr.sa(), addr.size());
    if (btpro::code::fail == rc)
    {
        if (!btpro::socket::inprogress())
            throw std::system_error(btpro::net::error_code(), "::connect");

        if (!socket_ready_write(socket, timeout))
            throw std::runtime_error("connect timeout");
    }
}

btpro::socket try_connect(btpro::ip::addr addr, int timeout)
{
    btpro::socket sock;
    // сокет создается неблокируемым
    sock.create(addr.family(), btpro::sock_stream);
    connect_sync(sock, addr, timeout);
    return sock;
}

btpro::socket try_gethostname_connect(const std::string& host, 
    const std::string& port, int timeout)
{
    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "connection: resolve connect to "sv;
        text += host, text += ':', text += port;
        return text;
    });

    addrinfo *result = nullptr;
    addrinfo hints{0, AF_UNSPEC, SOCK_STREAM, 0, 0, nullptr, nullptr, nullptr};
    auto rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (0 != rc) 
    {
        std::string msg{"getaddrinfo: "sv};
        msg += std::to_string(rc);
        msg += ' ';
        msg += host;

        if (rc == EAI_SYSTEM)
            throw std::system_error(btpro::net::error_code(), msg);
            
        std::string text(gai_strerror(rc));
        text += ' '; text += msg;
        throw std::runtime_error(text);
    }
    
    using holder_type = std::unique_ptr<addrinfo, void(*)(addrinfo*)>;
    holder_type a(result, [](addrinfo* ptr){
        freeaddrinfo(ptr);
    });

    if (nullptr == result)
    {
        std::string text{"unable to connect getaddrinfo: "sv};
        text += host;
        throw std::runtime_error(text);
    }

    auto addr = btpro::ip::addr::create(result->ai_addr, result->ai_addrlen);
    return try_connect(std::move(addr), timeout);
}

btpro::socket connection::create_connection(const btpro::uri& u, int timeout)
{
    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "connection: connect to "sv;
        text += u.addr_port(stomp_def);
        return text;
    });

    try
    {
        return try_connect(btpro::sock_addr{u.addr_port(stomp_def)}, timeout);
    }
    catch (const std::exception& e)
    {
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "connection: "sv;
            text += e.what();
            return text;
        });
    }

    return try_gethostname_connect(std::string{u.host()}, 
        std::to_string(u.port(stomp_def)), timeout);
}

// подключаемся только на локалхост
void connection::connect(const btpro::uri& u)
{
    CAPSTOMP_STATE(2);

    std::hash<std::string_view> hf;
    auto [login, passcode] = u.auth();
    auto passhash = hf(passcode);
    // проверяем было ли откличючение и совпадает ли пароль
    if (!(connected() && (passhash == passhash_)))
    {
        // закроем сокет
        close();

        // резолвим адрес если нужно
        socket_ = create_connection(u, static_cast<int>(conf::timeout()));

        destination_ = u.fragment();

        logon(u);

        // сохраняем пароль
        passhash_ = passhash;
    }

    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "connection: is connnected to "sv;
        text += u.addr_port(stomp_def);
        text += " socket="sv;
        text += std::to_string(socket_.fd());
        return text;
    });

    // начинаем транзакцию
    begin();
}

bool connection::connected()
{
    // жив ли сокет
    if (!socket_.good())
        return false;

    while (ready_read(0))
    {
        if (!read_stomp("connected"sv))
        {
            close();
            return false;
        }
    }

    return true;
}

void connection::set(connection_id_type self) noexcept
{
    self_ = self;
}

void connection::set(transaction_id_type id) noexcept
{
    capst_journal.trace([&, id]{
        std::string text;
        text.reserve(64);
        text += "connection: socket="sv;
        text += std::to_string(socket_.fd());
        text += " set deferred transaction:"sv;
        text += id->id();
        return text;
    });

    transaction_ = id;
    transaction_id_ = id->id();
}

void connection::logon(const btpro::uri& u)
{
    CAPSTOMP_STATE(3);

    auto path = u.rpath();

    if (path.empty())
        path = "/"sv;

    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "logon user="sv;
        auto [user, _p] = u.auth();
        text += user;
        text += " vhost="sv;
        text += path;
        text += " destination="sv;
        text += destination_;
        text += " socket="sv;
        text += std::to_string(socket_.fd());
        if (!transaction_id_.empty())
        {
            text += " transaction:"sv;
            text += transaction_id_;
        }
        return text;
    });

    auto [login, passcode] = u.auth();
    send(stompconn::logon(path, login, passcode));
    read("logon"sv);

    // должна быть получена сессия
    if (stomplay_.session().empty())
        throw std::runtime_error("stomplay: no session");
}

void connection::begin()
{
    CAPSTOMP_STATE(4);

    ++request_count_;

    // если транзакций нет - выходим
    if (!conf_.transaction())
        return;

    // создаем транзакцию
    set(pool_.create_transaction(self_));

    // начинаем транзакцию
    send(stompconn::begin(transaction_id_), is_receipt());
    read("begin"sv);
}

void connection::commit_transaction(transaction_type& transaction, bool receipt)
{
    auto transaction_id = transaction.id();
    auto connection_id = transaction.connection();

    try
    {
#ifdef CAPSTOMP_STATE_DEBUG
        connection_id->set_state(9);
#endif
        if (receipt)
        {
            connection_id->send(stompconn::commit(transaction_id), receipt);
            connection_id->read("commit_transaction"sv);
        }
        else
        {
            if (connection_id->connected())
            {
                // если коммитим несколько транзакций
                // тогда ожидаем подтверждение каждой
                connection_id->send(stompconn::commit(transaction_id), receipt);
                connection_id->read("commit_transaction"sv);
            }
            else
            {
                capst_journal.cerr([&]{
                    std::string text;
                    text.reserve(64);
                    text += "error commit: "sv;
                    text += transaction_id;
                    text += " - connecton lost"sv;
                    return text;
                });
            }
        }
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "error commit: "sv;
            text += transaction_id;
            text += " - "sv;
            text += e.what();
            return text;
        });
    }
    catch (...)
    {
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "error commit: "sv;
            text += transaction_id;
            return text;
        });
    }

    if (connection_id != self_)
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction:"sv;
            text += transaction_id;
            text += " release deffered"sv;
            return text;
        });

        pool_.release(connection_id);
    }
}

std::size_t connection::commit(transaction_store_type transaction_store)
{
    CAPSTOMP_STATE(8);

    auto rc = transaction_store.size();

    if (rc > 1)
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: "sv;
            text += transaction_id_;
            text += " socket="sv;
            text += std::to_string(socket_.fd());
            text += " commit multiple transactions:"sv;
            text += std::to_string(rc);
            return text;
        });
    }

    // если транзакция одна то используем флаг подтверждений из конфига
    // если несколько то подтверждаем все
    bool receipt = (rc == 1) ? is_receipt() : true;

    for (auto& transaction : transaction_store)
        commit_transaction(transaction, receipt);

    return rc;
}

bool connection::ready_read(int timeout)
{
    return (POLLIN & ready(POLLIN, timeout)) > 0;
}

int connection::ready(short int events, int timeout)
{
    return socket_poll(socket_, events, timeout);
}

void connection::read(std::string_view marker)
{
    while (!receipt_received_)
    {
        // ждем события чтения
        // таймаут на разовое чтение
        if (ready_read(static_cast<int>(conf::timeout())))
        {
            if (!read_stomp(marker))
            {
                close();

                if (!error_.empty())
                {
                    error_ = "disconnect: "sv;
                    error_ += marker;
                }

                receipt_received_ = true;
            }
        }
        else
            throw std::runtime_error(std::string("timeout: ") + marker.data());
    }

    // не должно быть ошибок
    if (!error_.empty())
        throw std::runtime_error(error_);
}

bool connection::read_stomp(std::string_view marker)
{
    // читаем
    char input[2048];
    auto rc = ::recv(socket_.fd(), input, sizeof(input), 0);
    if (btpro::code::fail == rc)
        throw std::system_error(btpro::net::error_code(),
                                std::string("recv: ") + marker.data());

    // парсим если чтото вычитали
    if (rc)
    {
        auto size = static_cast<std::size_t>(rc);
        if (size != stomplay_.parse(input, size))
            throw std::runtime_error(std::string("stomp parse: ") + marker.data());

        return true;
    }

    // иначе был дисконнект
    return false;
}

void connection::commit()
{
    // если была транзакиця
    // пытаемся ее закомитить
    if (with_transaction())
    {
        auto count = commit(pool_.get_uncommited(transaction_));
        if (count)
        {
            // если что-то коммитили
            // то релизим и себя
            // возможно это уничтожит этот объект
            // дальше им пользоваться уже нельзя

            if (count > 1)
            {
                capst_journal.cout([&]{
                    std::string text;
                    text.reserve(64);
                    text += "connection: transaction:"sv;
                    text += transaction_id_;
                    text += " release end"sv;
                    return text;
                });
            }

            release();
        }
        else
            CAPSTOMP_STATE(12);
    }
    else
        release();
}

void connection::force_commit()
{
    capst_journal.cerr([&]{
        std::string text;
        text.reserve(64);
        text += "force commit transaction:"sv;
        text += transaction_id_;
        return text;
    });

    commit_transaction(*transaction_, true);
}

void connection::release()
{
    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "connection:"sv;
        if (!transaction_id_.empty())
        {
            text += " transaction:"sv;
            text += transaction_id_;
        }
        text += " release"sv;
        text += " socket="sv;
        text += std::to_string(socket_.fd());
        return text;
    });

    CAPSTOMP_STATE(10);

    transaction_id_.clear();

    if (request_count_ >= conf::request_limit())
        request_count_ = std::size_t();

    // возможно это уничтожит этот объект
    // дальше им пользоваться уже нельзя
    pool_.release(self_);
}

bool connection::is_receipt() noexcept
{
    auto receipt = conf_.receipt();
    return (!receipt) ?
        request_count_ >= conf::request_limit() : receipt;
}

void connection::trace_frame(std::string frame)
{
    capst_journal.trace([&]{
        std::replace(frame.begin(), frame.end(), '\n', ' ');
        std::string text;
        text.reserve(64);
        text += "connection: "sv;
        text += frame;
        return text;
    });
}

void connection::trace_packet(const stompconn::packet& packet)
{
    if (!packet)
    {
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "connection error: "sv;
            if (!transaction_id_.empty())
            {
                text += " transaction:"sv;
                text += transaction_id_;
            }
            text += ' ';
            text += error_;
            return text;
        });
    }
    else
    {
        capst_journal.trace([&]{
            auto dump = packet.dump();
            std::replace(dump.begin(), dump.end(), '\n', ' ');
            std::string text;
            text.reserve(64);
            text += "connection: "sv;
            text += dump;
            return text;
        });
    }
}

std::size_t connection::send_content(stompconn::send frame)
{
    CAPSTOMP_STATE(6);

    // используем ли таймстамп
    if (conf_.timestamp())
        frame.push(stompconn::header::time_since_epoch());

    if (conf_.persistent())
        frame.push(stompconn::header::persistent_on());

    auto receipt = is_receipt();
    // используется ли транзакция
    if (!transaction_id_.empty())
    {
        frame.push(stompconn::header::transaction(transaction_id_));

        // подтверждения не используются при между (begin и commit)
        // сбрасываем ожидание подтверждения
        receipt = false;
    }

    auto rc = send(std::move(frame), receipt);
    read("send_content"sv);
    return rc;
}

std::size_t connection::send(stompconn::buffer data)
{
    auto rc = data.size();
    do
    {
        auto ev = ready(POLLIN|POLLOUT,
            static_cast<int>(conf::timeout()));
        // сокет неожиданно стал доступен на запись
        // а мы ничего не ждем
        if (ev & POLLIN)
        {
            if (!read_stomp("send"sv))
            {
                close();

                if (!error_.empty())
                    error_ = "disconnect"sv;
            }

            throw std::runtime_error("protocol error");
        }

        if (ev & POLLOUT)
        {
            auto rc = data.write(socket_.fd());
            if (btpro::code::fail == rc)
            {
                // проверяем на блокировку операции
                if (!btpro::socket::wouldblock())
                    throw std::system_error(btpro::net::error_code(), "send");
            }
        }
        else
            throw std::runtime_error("send timeout");
    }
    while (!data.empty());

    // число отправок
    ++total_count_;

    return rc;
}

std::size_t connection::send(stompconn::logon frame)
{
    // опрация логона всегда ожидает ответ
    // тк выполняется после подключения
    // запускаем ожидание приема
    receipt_received_ = false;

    return send(frame.data());
}

} // namespace capst
