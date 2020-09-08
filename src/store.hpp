#pragma once

#include "pool.hpp"

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace capst {

class store
{
    using store_type = std::list<pool>;
    using iterator = store_type::iterator;
    using index_type = std::unordered_map<std::string, iterator>;
    using lock = std::lock_guard<std::mutex>;

    std::mutex mutex_{};
    store_type store_{};
    index_type index_{};

    store() = default;
public:

    pool& get(const std::string& uri);

    static store& inst() noexcept;
};

} // namespace capst
