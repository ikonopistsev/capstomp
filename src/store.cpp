#include "store.hpp"
#include "pool.hpp"
#include "journal.hpp"

namespace capst {


pool& store::get(const std::string& uri)
{
    static constexpr auto max_pool_count = std::size_t{CAPSTOMP_MAX_POOL_COUNT};

    lock l(mutex_);

    auto f = index_.find(uri);
    if (f != index_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text = "store: get existing pool";
            return text;
        });
#endif
        return *f->second;
    }

    if (store_.size() >= max_pool_count)
        throw std::runtime_error("store: max_pool_count=" +
                                 std::to_string(max_pool_count));

    store_.emplace_front();
    index_[uri] = store_.begin();

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text = "store: create new pool";
            return text;
        });
#endif
    return store_.front();
}

store& store::inst() noexcept
{
    static store i;
    return i;
}

} // namespace capst
