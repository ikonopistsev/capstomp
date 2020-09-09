#pragma once

#include "uri.hpp"
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

    using transaction_type =
        transaction<btdef::util::basic_text<char, 64>, connection_id_type>;
    using transaction_store_type = std::list<transaction_type>;
    using transaction_id_type = transaction_store_type::iterator;

private:
    pool& pool_;
    // указатель на позицию в пуле коннектов
    connection_id_type self_{};
    // указатель на выполняемую транзакцию
    transaction_id_type transaction_{};
    // connection string
    std::string uri_{};
    std::string destination_{};
    std::string transaction_id_{};
    // stomp protocol error
    std::string error_{};
    std::size_t receipt_id_{};
    bool wait_receipt_{false};
    std::size_t transaction_receipt_{};

    // connection timeout
    int timeout_{10000};

    btpro::socket socket_{};
    stompconn::stomplay stomplay_{};

public:

    connection(pool& pool);

    ~connection();

    void close() noexcept;

    // lock and connect
    void connect(std::string_view uri);

    template<class F>
    std::size_t send(F frame)
    {
        return frame.write_all(socket_);
    }

    template<class T, class F>
    std::size_t send(T frame, const std::string& receipt_id, F fn)
    {
        if (transaction_receipt_)
        {
            frame.push(stomptalk::header::receipt(receipt_id));

            stomplay_.add_handler(receipt_id, [&, fn](stompconn::packet packet){
                // завершаем ожидание квитанции
                wait_receipt_ = false;
                // вызываем
                fn(std::move(packet));
            });
        }

        return send(std::move(frame));
    }

    // управление таймаутом на получение ответа
    void set(int timeout) noexcept;

    // задание указателя на хранилище коннектов
    void set(connection_id_type self) noexcept;

    // задание указателя на выполняемую транзакцию
    void set(transaction_id_type transaction_id) noexcept;

    // выполнить коммиты и вернуть соединение
    void commit();

    // вернуть соединение
    void release();

    std::string_view destination() const noexcept
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

    std::string create_receipt_id(std::string_view action);

    void read_receipt(int timeout);

private:

    bool connected();

    void logon(const uri& u, int timeout);

    void begin();

    std::size_t commit(transaction_store_type transaction_store);

    bool ready_read(int timeout);

    void read_logon(int timeout);

    bool read_stomp();

};

} // namespace cs
