#pragma once

#include "settings.hpp"
#include "connection.hpp"
#include "transaction.hpp"

#include <list>
#include <condition_variable>

namespace capst {

class pool
{
public:
    using list_type = connection::list_type;
    using connection_id_type = connection::connection_id_type;

private:
    std::mutex mutex_{};
    using lock = std::lock_guard<std::mutex>;

    list_type active_{};
    list_type ready_{};

    // имя пула
    std::string name_{};
    // номер последовательности транзакции в пуле
    std::size_t transaction_seq_{};

    using transaction_store_type = connection::transaction_store_type;
    using transaction_id_type = connection::transaction_id_type;
    transaction_store_type transaction_store_{};

    std::string create_transaction_id();

    void destroy();

    void release_connection(connection_id_type connection_id);

public:
    pool();

    pool(const std::string& name);

    void clear() noexcept;

    std::size_t active_size()
    {
        lock l(mutex_);
        return active_.size();
    }

    connection& get(const settings& conf);

    void release(connection_id_type connection_id);

    transaction_id_type create_transaction(connection_id_type connection_id);

    // подтверждаем свою операцию и возвращаем список коммитов
    transaction_store_type get_uncommited(transaction_id_type transaction_id);

    std::size_t force_commit();

    std::string json();

private:

    std::string json_arr(list_type& list);
};

} // namespace capst
