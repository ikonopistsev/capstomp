#pragma once

#include "journal.hpp"
#include "settings.hpp"
#include "transaction.hpp"

#include "stompconn/stomplay.hpp"
#include "stompconn/frame.hpp"

#include "btpro/socket.hpp"
#include "btpro/buffer.hpp"

#include <list>
#include <mutex>
#include <cassert>
#include <atomic>

namespace capst {

class pool;
class connection
{
public:
    using list_type = std::list<connection>;
    using connection_id_type = list_type::iterator;

    using transaction_type = transaction<std::string, connection_id_type>;
    using transaction_store_type = std::list<transaction_type>;
    using transaction_id_type = transaction_store_type::iterator;

private:
    pool& pool_;
    // настройки коннекта
    settings conf_;
    // указатель на позицию в пуле коннектов
    connection_id_type self_{};

    // название транзакции
    std::string transaction_id_{};
    // указатель на выполняемую транзакцию
    transaction_id_type transaction_{};

    // stomp protocol error
    std::string error_{};
    std::size_t receipt_seq_{};
    bool receipt_received_{true};

    std::size_t passhash_{};
    std::string destination_{};

    btpro::socket socket_{};
    stompconn::stomplay stomplay_{};

    std::size_t total_count_{};
    std::size_t request_count_{};
#ifdef CAPSTOMP_STATE_DEBUG
    std::atomic<std::size_t> state_{};
#endif // CAPSTOMP_STATE_DEBUG
public:

    connection(pool& pool);

    ~connection();

    void close() noexcept;

    void init(const settings& conf) noexcept;

    void connect(const btpro::uri& u);

    std::size_t send_content(stompconn::send frame);

    // задание указателя соединение в хранилище подключений
    void set(connection_id_type self) noexcept;

    // задание указателя на выполняемую транзакцию
    void set(transaction_id_type transaction_id) noexcept;

    // выполнить коммиты и вернуть соединение
    void commit();

    // выполнить комиты без проверки последовательности
    void force_commit();

    // вернуть соединение
    void release();

    bool good() const noexcept
    {
        return socket_.good();
    }

    btpro::socket socket() const noexcept
    {
        return socket_;
    }

#ifdef CAPSTOMP_STATE_DEBUG
    void set_state(std::size_t n)
    {
        state_ = n;
    }

    std::size_t state() const noexcept
    {
        return state_;
    }
#endif // CAPSTOMP_STATE_DEBUG

    std::size_t total_count() const noexcept
    {
        return total_count_;
    }

    const std::string& destination() const noexcept
    {
        return destination_;
    }

    std::string_view transaction_id() const noexcept
    {
        return transaction_id_;
    }

    bool with_transaction() const noexcept
    {
        return !transaction_id_.empty();
    }

    std::string_view error() const noexcept
    {
        return error_;
    }

private:

    bool connected();

    void logon(const btpro::uri& u);

    void begin();

    void commit_transaction(transaction_type& transaction, bool receipt);

    std::size_t commit(transaction_store_type transaction_store);

    bool ready_read(int timeout);

    int ready(short int events, int timeout);

    void read(std::string_view marker);

    bool read_stomp(std::string_view marker);

    std::size_t send(stompconn::buffer buf);

    std::size_t send(stompconn::logon frame);

    bool is_receipt() noexcept;

    void trace_frame(std::string frame);

    void trace_packet(const stompconn::packet& packet);

    template<class T>
    std::size_t send(T frame, bool receipt)
    {
        if (receipt)
        {
            // запускаем ожидание приема
            receipt_received_ = false;

            stomplay_.add_handler(frame, [&](stompconn::packet packet){
                // квитанция получена в любом случае
                receipt_received_ = true;

                if (!packet)
                    error_ = packet.payload().str();

                trace_packet(packet);
            });
        }
        else
        {
            receipt_received_ = true;
        }

        if (capst_journal.allow_trace())
            trace_frame(frame.str());

        return send(frame.data());
    }

    template<class T>
    std::size_t send(T frame)
    {
        return send(std::move(frame), is_receipt());
    }
};

} // namespace cs
