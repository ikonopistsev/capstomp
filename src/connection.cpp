#include "connection.hpp"
#include "journal.hpp"
#include "pool.hpp"
#include "conf.hpp"

#include "btpro/sock_addr.hpp"

#include <poll.h>
#include <event2/keyvalq_struct.h>

using namespace std::literals;

namespace capst {

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
            return text;
        });
    }

    socket_.close();
    destination_.clear();
    passhash_ = std::size_t();
    request_count_ = std::size_t();
    error_.clear();
}

void connection::init() noexcept
{
    transaction_id_.clear();
    error_.clear();
}

int socket_poll(btpro::socket socket, short int events, int timeout)
{
    auto ev = pollfd{
        socket.fd(), events, {}
    };

    auto rc = poll(&ev, 1, timeout);
    if (btpro::code::fail == rc)
        throw std::system_error(btpro::net::error_code(), "poll");

#ifdef CAPSTOMP_TRACE_LOG
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
#endif

    return ev.revents;
}

bool socket_ready_write(btpro::socket socket, int timeout)
{
    return (POLLOUT & socket_poll(socket, POLLOUT, timeout)) > 0;
}

void connect_sync(btpro::socket socket, btpro::ip::addr addr, int timeout)
{
    auto rc = ::connect(socket.fd(), addr.sa(), addr.size());
    if (btpro::code::fail != rc)
    {
        if (!btpro::socket::inprogress())
            throw std::system_error(btpro::net::error_code(), "::connect");

        if (!socket_ready_write(socket, timeout))
            throw std::runtime_error("connect timeout");
    }
}

// подключаемся только на локалхост
void connection::connect(const uri& u)
{
    set_state(2);

    std::hash<std::string_view> hf;
    auto passhash = hf(u.passcode());
    // проверяем было ли откличючение и совпадает ли пароль
    if (!(connected() && (passhash == passhash_)))
    {
        // закроем сокет
        close();
        // парсим адрес
        // и коннектимся на новый сокет
        btpro::sock_addr addr(u.addr());

        btpro::socket socket;
        // сокет создается неблокируемым
        socket.create(addr.family(), btpro::sock_stream);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "connection: connect to "sv;
            text += u.addr();
            text += " socket="sv;
            text += std::to_string(socket.fd());
            return text;
        });
#endif // CAPSTOMP_TRACE_LOG

        connect_sync(socket, addr, static_cast<int>(conf::timeout()));

        socket_ = socket;

        destination_ = u.fragment();

        logon(u);

        // сохраняем пароль
        passhash_ = passhash;
    }

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
        if (!read_stomp())
        {
            close();
            return false;
        }
    }

    return true;
}

void connection::set(const connection_settings& conf)
{
    conf_ = conf;
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

void connection::logon(const uri& u)
{
    set_state(3);

    auto path = u.rpath();

    if (path.empty())
        path = "/"sv;

#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "logon user="sv;
        text += u.user();
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
#endif // CAPSTOMP_TRACE_LOG

    send(stompconn::logon(path, u.user(), u.passcode()));
    read();

    // должна быть получена сессия
    if (stomplay_.session().empty())
        throw std::runtime_error("stomplay: no session");
}

void connection::begin()
{
    set_state(4);

    ++request_count_;

    // если транзакций нет - выходим
    if (!conf_.transaction())
        return;

    send(stompconn::begin(transaction_id_), is_receipt());
    read();
}

void connection::commit_transaction(transaction_type& transaction, bool receipt)
{
    auto transaction_id = transaction.id();
    auto connection_id = transaction.connection();

    try
    {
        connection_id->set_state(9);

        if (receipt)
        {
            connection_id->send(stompconn::commit(transaction_id), receipt);
            connection_id->read();
        }
        else
        {
            if (connection_id->connected())
            {
                // если коммитим несколько транзакций
                // тогда ожидаем подтверждение каждой
                connection_id->send(stompconn::commit(transaction_id), receipt);
                connection_id->read();
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
    set_state(8);

    auto rc = transaction_store.size();

    if (rc)
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: "sv;
            text += transaction_id_;
            text += " socket="sv;
            text += std::to_string(socket_.fd());
            text += "commit multiple transactions:"sv;
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

void connection::read()
{
    while (!receipt_received_)
    {
        // ждем события чтения
        // таймаут на разовое чтение
        if (ready_read(static_cast<int>(conf::timeout())))
        {
            if (!read_stomp())
            {
                close();

                if (!error_.empty())
                    error_ = "disconnect"sv;

                receipt_received_ = true;
            }
        }
        else
            throw std::runtime_error("timeout");
    }

    // не должно быть ошибок
    if (!error_.empty())
        throw std::runtime_error(error_);
}

bool connection::read_stomp()
{
    // читаем
    char input[2048];
    auto rc = ::recv(socket_.fd(), input, sizeof(input), 0);
    if (btpro::code::fail == rc)
        throw std::system_error(btpro::net::error_code(), "recv");

    // парсим если чтото вычитали
    if (rc)
    {
        auto size = static_cast<std::size_t>(rc);
        if (size != stomplay_.parse(input, size))
            throw std::runtime_error("stomp parse");

        return true;
    }

    // иначе был дисконнект
    return false;
}

void connection::commit()
{
    if (conf_.transaction())
    {
        if (commit(pool_.get_uncommited(transaction_)) > 0)
        {
            // если что-то коммитили
            // то релизим и себя
            // возможно это уничтожит этот объект
            // дальше им пользоваться уже нельзя
            release();
        }
        else
            set_state(12);
    }
    else
        release();
}

void connection::release()
{
#ifdef CAPSTOMP_TRACE_LOG
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
        return text;
    });
#endif
    set_state(10);

    transaction_id_.clear();

    if (request_count_ >= conf::request_limit())
        request_count_ = std::size_t();

    // возможно это уничтожит этот объект
    // дальше им пользоваться уже нельзя
    pool_.release(self_);
}

std::string connection::create_receipt_id(std::string_view transaction_id)
{
    std::string rc;
    rc.reserve(64);
    rc = std::to_string(++receipt_seq_);
    rc += '#';
    rc += std::to_string(socket_.fd());
    if (!transaction_id.empty())
    {
        rc += 'T';
        rc += transaction_id;
    }
    return rc;
}

bool connection::is_receipt() noexcept
{
    auto receipt = conf_.receipt();
    return (!receipt) ?
        request_count_ >= conf::request_limit() : receipt;
}

#ifdef CAPSTOMP_TRACE_LOG
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
#endif //

void connection::trace_packet(const stompconn::packet& packet,
    const std::string& receipt_id)
{
    if (!packet)
    {
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "connection error: receipt:"sv;
            text += receipt_id;
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
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            auto dump = packet.dump();
            std::replace(dump.begin(), dump.end(), '\n', ' ');
            std::string text;
            text.reserve(64);
            text += "connection: "sv;
            text += dump;
            return text;
        });
#endif
    }
}

std::size_t connection::send_content(stompconn::send frame)
{
    set_state(6);

    // используем ли таймстамп
    if (conf_.timestamp())
        frame.push(stomptalk::header::time_since_epoch());

    if (conf_.persistent())
        frame.push(stomptalk::header::persistent_on());

    auto delivery_mode = conf_.delivery_mode();
    if (delivery_mode)
        frame.push(stomptalk::header::delivery_mode(delivery_mode));

    auto receipt = is_receipt();
    // используется ли транзакция
    if (!transaction_id_.empty())
    {
        frame.push(stomptalk::header::transaction(transaction_id_));

        // подтверждения не используются при между (begin и commit)
        // сбрасываем ожидание подтверждения
        receipt = false;
    }

    auto rc = send(std::move(frame), receipt);
    read();
    return rc;
}

std::size_t connection::send(btpro::buffer data)
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
            if (!read_stomp())
            {
                close();

                if (!error_.empty())
                    error_ = "disconnect"sv;
            }

            throw std::runtime_error("protocol error");
        }

        if (ev & POLLOUT)
        {
            auto rc = data.write(socket_);
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
