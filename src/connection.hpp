#pragma once

#include "journal.hpp"
#include "endpoint.hpp"
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

    using transaction_type =
        transaction<btdef::util::basic_text<char, 64>, connection_id_type>;
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

    // lock and connect
    void connect(const uri& uri);

    template<class F>
    std::size_t send(F frame)
    {
        return frame.write_all(socket_);
    }

    std::size_t send(stompconn::logon frame)
    {
        // опрация логона всегда ожидает ответ
        // тк выполняется после подключения
        // запускаем ожидание приема
        receipt_received_ = false;

        // отправляем данные
        return frame.write_all(socket_);
    }

    std::size_t send_content(stompconn::send frame);

    template<class T, class F>
    std::size_t send(T frame, const std::string& receipt_id, F fn)
    {
        // если есть чего ожидать
        if (!receipt_id.empty())
        {
            frame.push(stomptalk::header::receipt(receipt_id));

            // запускаем ожидание приема
            receipt_received_ = false;

            stomplay_.add_handler(receipt_id, [&, fn](stompconn::packet packet){
                // квитанция получена
                receipt_received_ = true;
                // вызываем
                fn(std::move(packet));
            });
        }

        return send(std::move(frame));
    }

    template<class F>
    std::size_t send(stompconn::send frame, const std::string& receipt_id, F fn)
    {
        // используем ли таймстамп
        if (conf_.timestamp())
            frame.push(stomptalk::header::time_since_epoch());

        // используется ли транзакция
        if (!transaction_id_.empty())
            frame.push(stomptalk::header::transaction(transaction_id_));

        // если есть чего ожидать
        if (!receipt_id.empty())
        {
            frame.push(stomptalk::header::receipt(receipt_id));

            // запускаем ожидание приема
            receipt_received_ = false;

            stomplay_.add_handler(receipt_id, [&, fn](stompconn::packet packet){
                // квитанция получена
                receipt_received_ = true;
                // вызываем
                fn(std::move(packet));
            });
        }

        return send(std::move(frame));
    }

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

    std::string create_receipt_id(std::string_view action);

private:

    bool connected();

    void logon(const uri& u);

    void begin();

    std::size_t commit(transaction_store_type transaction_store);

    bool ready_read(int timeout);

    void read();

    bool read_stomp();

};

} // namespace cs
