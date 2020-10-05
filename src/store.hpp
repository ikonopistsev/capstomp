#pragma once

#include "pool.hpp"

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace capst {

class store
{
    using store_type = std::unordered_map<std::string, pool>;
    using lock = std::lock_guard<std::mutex>;

    std::mutex mutex_{};
    store_type store_{};

    store() = default;

    pool& select_pool(const uri& u, const std::string& forced_pool);

public:

    connection& get(const uri& u);

    std::string json();

    void erase(const std::string& name);

    void commit(const std::string& name);

    void clear();

    static store& inst() noexcept;
};

} // namespace capst
