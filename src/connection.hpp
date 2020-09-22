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
    connection_settings conf_;
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

    std::string destination_{};

    btpro::socket socket_{};
    stompconn::stomplay stomplay_{};

public:

    connection(pool& pool);

    ~connection();

    void close() noexcept;

    void init() noexcept;

    // lock and connect
    void connect(const uri& u);

    std::size_t send_content(stompconn::send frame);

    void set(const connection_settings& conf);

    // задание указателя на хранилище коннектов
    void set(connection_id_type self) noexcept;

    // задание указателя на выполняемую транзакцию
    void set(transaction_id_type transaction_id) noexcept;

    // выполнить коммиты и вернуть соединение
    void commit();

    // вернуть соединение
    void release();

    const std::string& destination() const noexcept
    {
        return destination_;
    }

    std::string_view transaction_id() const noexcept
    {
        return transaction_id_;
    }

    std::string_view error() const noexcept
    {
        return error_;
    }

private:

    bool connected();

    void logon(const uri& u);

    void begin();

    void commit_transaction(transaction_type& transaction, bool single);

    std::size_t commit(transaction_store_type transaction_store);

    bool ready_read(int timeout);

    void read();

    bool read_stomp();

    std::size_t send(stompconn::logon frame);

    std::string create_receipt_id(std::string_view transaction_id);

#ifdef CAPSTOMP_TRACE_LOG
    void trace_frame(std::string frame);
#endif //

    void trace_packet(const stompconn::packet& packet,
                      const std::string& receipt_id);

    template<class T>
    std::size_t send(T frame, bool receipt)
    {
        if (receipt)
        {
            auto receipt_id = create_receipt_id(transaction_id_);
            frame.push(stomptalk::header::receipt(receipt_id));

            // запускаем ожидание приема
            receipt_received_ = false;

            stomplay_.add_handler(receipt_id,
                                  [&, receipt_id](stompconn::packet packet){
                // квитанция получена в любом случае
                receipt_received_ = true;

                if (!packet)
                    error_ = packet.payload().str();

                trace_packet(packet, receipt_id);
            });
        }
        else
        {
            receipt_received_ = true;
        }

#ifdef CAPSTOMP_TRACE_LOG
        trace_frame(frame.str());
#endif //

        return frame.write_all(socket_);
    }

    template<class T>
    std::size_t send(T frame)
    {
        return send(std::move(frame), conf_.receipt());
    }
};

} // namespace cs
