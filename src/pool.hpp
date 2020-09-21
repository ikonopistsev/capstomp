#pragma once

#include "settings.hpp"
#include "connection.hpp"
#include "transaction.hpp"

#include <list>
#include <condition_variable>

namespace capst {

class pool
{
    using list_type = connection::list_type;
    using connection_id_type = connection::connection_id_type;

    std::mutex mutex_{};
    std::condition_variable cv_{};
    using lock = std::lock_guard<std::mutex>;
    using unique_lock = std::unique_lock<std::mutex>;

    list_type active_{};
    list_type ready_{};

    // имя пула
    std::string name_{};
    // номер последовательности транзакции в пуле
    std::size_t sequence_{};

    using transaction_store_type = connection::transaction_store_type;
    using transaction_id_type = connection::transaction_id_type;
    transaction_store_type transaction_store_{};

    std::string create_transaction_id();

public:
    pool();

    connection& get(const settings& conf);

    void release(connection_id_type connection_id);

    // подтверждаем свою операцию и возвращаем список коммитов
    transaction_store_type get_uncommited(transaction_id_type transaction_id);

    std::string_view name() const noexcept
    {
        return name_;
    }

};

} // namespace capst
