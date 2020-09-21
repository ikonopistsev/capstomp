#pragma once

#include "pool.hpp"

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace capst {

class store
{
    using endpoint_type = endpoint<std::string>;
    using store_type = std::unordered_map<endpoint_type, pool, endpoint_type::hf>;
    using lock = std::lock_guard<std::mutex>;

    std::mutex mutex_{};
    store_type store_{};

    store() = default;

    pool& select_pool(const uri& u);

public:

    connection& get(const uri& u);

    static store& inst() noexcept;
};

} // namespace capst
