#include "connection.hpp"
#include "journal.hpp"
#include "pool.hpp"

#include "btpro/sock_addr.hpp"

#include <poll.h>
#include <event2/keyvalq_struct.h>

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

    socket_.close();

    destination_.clear();
}

connection::connection(pool& pool)
    : pool_(pool)
{
    stomplay_.on_logon([&](stompconn::packet logon){
        receipt_received_ = true;
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
        receipt_received_ = true;
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
void connection::connect(const uri& u)
{
    // проверяем связь и было ли отключение
    if (!connected())
    {
        // парсим адрес
        // и коннектимся на новый сокет
        btpro::sock_addr addr(u.addr());

        auto fd = ::socket(addr.family(), SOCK_STREAM, 0);
        if (btpro::code::fail == fd)
            throw std::system_error(btpro::net::error_code(), "socket");

        auto res = ::connect(fd, addr.sa(), addr.size());
        if (btpro::code::fail == res)
            throw std::system_error(btpro::net::error_code(), "connect");

        socket_.attach(fd);

        destination_ = u.fragment();
        // сбрасываем ошибку
        error_.clear();

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
            socket_.close();
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
                text += " transaction_id="sv;
                text += transaction_id_;
            }
            return text;
        });
#endif

    send(stompconn::logon(path, u.user(), u.passcode()));

    // после подключения всегда ожидаем ответа
    read();

    // не должно быть ошибок
    if (!error_.empty())
        throw std::runtime_error(error_);

    // должна быть получена сессия
    if (stomplay_.session().empty())
        throw std::runtime_error("stomplay: no session");
}

void connection::begin()
{
    // если транзакций нет - выходим
    if (transaction_id_.empty())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text += "connection: transaction off"sv;
            return text;
        });
#endif
        return;
    }

    stompconn::begin frame(transaction_id_);

    // create_receipt_id может вернуть пустую строку
    // когда прием квитанций отключен
    auto receipt_id = create_receipt_id(transaction_id_);

#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.cout([&]{
        std::string text;
        text.reserve(64);
        text += "connection begin: transaction_id="sv;
        text += transaction_id_;
        if (!receipt_id.empty())
        {
            text += " receipt_id="sv;
            text += receipt_id;
        }
        return text;
    });
#endif

    // для проброса receipt_id надо захватить его по значению
    send(std::move(frame), receipt_id,
         [&, receipt_id](stompconn::packet receipt) {
            if (!receipt)
            {
                capst_journal.cerr([&]{
                    std::string text;
                    text.reserve(64);
                    text += "begin error: "sv;
                    text += "transaction_id="sv;
                    text += transaction_id_;
                    text += " "sv;
                    text += receipt.payload().str();
                    return text;
                });
            }
            else
            {
#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "begin ok: "sv;
                text += "transaction_id="sv;
                text += transaction_id_;
                text += " receipt_id="sv;
                text += receipt_id;
                return text;
            });
#endif //
            }
    });

    read();

    if (!error_.empty())
        throw std::runtime_error(error_);
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
            auto receipt_id = connection_id->create_receipt_id(transaction_id);

#ifdef CAPSTOMP_TRACE_LOG
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "connection commit: transaction_id="sv;
                text += transaction_id;
                if (!receipt_id.empty())
                {
                    text += " receipt_id="sv;
                    text += receipt_id;
                }
                return text;
            });
#endif

            connection_id->send(std::move(frame), receipt_id,
                [&, receipt_id](stompconn::packet receipt){
                    if (!receipt)
                    {
                        capst_journal.cerr([&, receipt_id]{
                            std::string text;
                            text.reserve(64);
                            text += "commit error: transaction_id=";
                            text += transaction_id;
                            text += " receipt_id="sv;
                            text += receipt_id;
                            return text;
                        });
                    }
                    else
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
            });

            connection_id->read();

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

void connection::read()
{
    while (!receipt_received_)
    {
        // ждем события чтения
        // таймаут на разовое чтение
        if (ready_read(conf_.timeout()))
        {
            if (!read_stomp())
            {
                socket_.close();

                if (!error_.empty())
                    error_ = "read_stomp disconnect"sv;

                receipt_received_ = true;
            }
        }
        else
            throw std::runtime_error("read_receipt timeout");
    }
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
    if (!conf_.transaction() || commit(pool_.get_uncommited(transaction_)) > 0)
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
        text += "connection: release"sv;
        if (!transaction_id_.empty())
        {
            text += " transaction_id="sv;
            text += transaction_id_;
        }
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
    if (conf_.receipt())
    {
        rc.reserve(64);
        rc = std::to_string(++receipt_seq_);
        rc += '#';
        rc += std::to_string(socket_.fd());
        if (!transaction_id.empty())
        {
            rc += 'T';
            rc += transaction_id;
        }
    }
    return rc;
}

std::size_t connection::send_content(stompconn::send frame)
{
    // используем ли таймстамп
    if (conf_.timestamp())
        frame.push(stomptalk::header::time_since_epoch());

    auto payload_size = frame.payload_size();
    auto receipt_id = create_receipt_id(transaction_id_);

    // используется ли транзакция
    if (!transaction_id_.empty())
    {
        frame.push(stomptalk::header::transaction(transaction_id_));

        // внутри транзакции (между begin и commit) не может быть подтверждений
        receipt_id.clear();
    }

#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.cout([&]{
        std::string text;
        text.reserve(64);
        text += "connection: send"sv;

        text += " content to destintation="sv;
        text += destination();

        auto id = transaction_id();
        if (!id.empty())
        {
            text += " transaction_id="sv;
            text += id;
        }

        if (!receipt_id.empty())
        {
            text += " receipt_id="sv;
            text += receipt_id;
        }

        if (payload_size)
        {
            text += " payload_size="sv;
            text += std::to_string(payload_size);
        }

        return text;
    });
#endif

    // если есть чего ожидать
    if (!receipt_id.empty())
    {
        frame.push(stomptalk::header::receipt(receipt_id));

        // запускаем ожидание приема
        receipt_received_ = false;

        stomplay_.add_handler(receipt_id, [&](stompconn::packet packet){
            // квитанция получена
            receipt_received_ = true;

            if (!packet)
            {
                error_ = packet.payload().str();

                capst_journal.cerr([&]{
                    std::string text;
                    text.reserve(64);
                    text += "connection send error:";

                    auto id = transaction_id();
                    if (!id.empty())
                    {
                        text += " transaction_id=";
                        text += id;
                    }

                    text += " receipt_id=";
                    text += receipt_id;

                    text += " destintation=";
                    text += destination();

                    text += " ";
                    text += error_;
                    return text;
                });
            }
            else
            {
#ifdef CAPSTOMP_TRACE_LOG
                capst_journal.cout([&]{
                    std::string text;
                    text.reserve(64);
                    text += "connection send ok:";

                    auto id = transaction_id();
                    if (!id.empty())
                    {
                        text += " transaction_id=";
                        text += id;
                    }

                    text += " receipt_id=";
                    text += receipt_id;

                    text += " destintation=";
                    text += destination();

                    return text;
                });
#endif
            }
        });
    }

    auto rc = frame.write_all(socket_);

    read();

    return rc;
}

} // namespace capst
