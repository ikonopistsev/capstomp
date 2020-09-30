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
    passcode_.clear();
    error_.clear();
}

void connection::init() noexcept
{
    transaction_id_.clear();
    error_.clear();
}

// подключаемся только на локалхост
void connection::connect(const uri& u)
{
    // проверяем было ли откличючение и совпадает ли пароль
    if (!(connected() && (u.passcode() == passcode_)))
    {
        // закроем сокет
        close();
        // парсим адрес
        // и коннектимся на новый сокет
        btpro::sock_addr addr(u.addr());

        auto fd = ::socket(addr.family(), SOCK_STREAM, 0);
        if (btpro::code::fail == fd)
            throw std::system_error(btpro::net::error_code(), "socket");

        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: connect to "sv;
            text += u.addr();
            return text;
        });

        auto res = ::connect(fd, addr.sa(), addr.size());
        if (btpro::code::fail == res)
            throw std::system_error(btpro::net::error_code(), "connect");

        socket_.attach(fd);

        destination_ = u.fragment();

        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: logon to destination="sv;
            text += destination_;
            return text;
        });

        logon(u);
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
    auto path = u.rpath();

    if (path.empty())
        path = "/"sv;

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: logon user="sv;
            text += u.user();
            text += " vhost="sv;
            text += path;
            if (!transaction_id_.empty())
            {
                text += " transaction:"sv;
                text += transaction_id_;
            }
            return text;
        });
#endif

    auto passcode = u.passcode();
    send(stompconn::logon(path, u.user(), passcode));
    read();

    // должна быть получена сессия
    if (stomplay_.session().empty())
        throw std::runtime_error("stomplay: no session");

    // сохраняем пароль
    passcode_ = passcode;
}

void connection::begin()
{
    // если транзакций нет - выходим
    if (!conf_.transaction())
        return;

    send(stompconn::begin(transaction_id_), conf_.receipt());
    read();
}

void connection::commit_transaction(transaction_type& transaction, bool single)
{
    auto transaction_id = transaction.id();
    auto connection_id = transaction.connection();

    try
    {
        if (connection_id->connected())
        {
            // если коммитим несколько транзакций
            // тогда ожидаем подтверждение каждой
            connection_id->send(stompconn::commit(transaction_id), !single);
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
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: transaction:"sv;
            text += transaction_id;
            text += " release deffered"sv;
            return text;
        });
#endif
        pool_.release(connection_id);
    }
}

std::size_t connection::commit(transaction_store_type transaction_store)
{
    auto rc = transaction_store.size();

    for (auto& transaction : transaction_store)
        commit_transaction(transaction, rc == 1);

    return rc;
}

bool connection::ready_read(int timeout)
{
    auto ev = pollfd{
        socket_.fd(), POLLIN, 0
    };

    auto rc = poll(&ev, 1, timeout);

    if (btpro::code::fail == rc)
        throw std::runtime_error("connection select");

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "connection: socket="sv;
            text += std::to_string(socket_.fd());
            text += " POOLIN="sv;
            text += std::to_string(rc);
            return text;
        });
#endif

    return 1 == rc;
}

void connection::read()
{
    auto read_timeout = conf::read_timeout();
    while (!receipt_received_)
    {
        // ждем события чтения
        // таймаут на разовое чтение
        if (ready_read(read_timeout))
        {
            if (!read_stomp())
            {
                close();

                if (!error_.empty())
                    error_ = "read_stomp disconnect"sv;

                receipt_received_ = true;
            }
        }
        else
            throw std::runtime_error("recv timeout");
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
        throw std::runtime_error("stomp recv");

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
    if (!conf_.transaction() ||
        commit(pool_.get_uncommited(transaction_)) > 0)
    {
        // если что-то коммитили
        // то релизим и себя
        // возможно это уничтожит этот объект
        // дальше им пользоваться уже нельзя
        release();
    }
}

void connection::release()
{
#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.cout([&]{
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

    transaction_id_.clear();

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

#ifdef CAPSTOMP_TRACE_LOG
void connection::trace_frame(std::string frame)
{
    capst_journal.cout([&]{
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
        capst_journal.cout([&]{
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
    // используем ли таймстамп
    if (conf_.timestamp())
        frame.push(stomptalk::header::time_since_epoch());

    auto receipt = conf_.receipt();
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

std::size_t connection::send(stompconn::logon frame)
{
    // опрация логона всегда ожидает ответ
    // тк выполняется после подключения
    // запускаем ожидание приема
    receipt_received_ = false;

    // отправляем данные
    return frame.write_all(socket_);
}

} // namespace capst
