#include "store.hpp"
#include "pool.hpp"
#include "journal.hpp"

using namespace std::literals;

namespace capst {

pool& store::select_pool(const uri& u)
{
    static constexpr auto pool_max = std::size_t{CAPSTOMP_MAX_POOL_COUNT};

    endpoint ep(u);

    lock l(mutex_);

    auto f = store_.find(ep);
    if (f != store_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text = "store: get existing pool";
            return text;
        });
#endif
        return f->second;
    }

    if (store_.size() >= pool_max)
        throw std::runtime_error("store: max_pool_count=" +
                                 std::to_string(pool_max));

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text = "store: create new pool";
            return text;
        });
#endif
    return store_[ep];
}

connection& store::get(const uri& u)
{
    // парсим настройки
    auto s = settings::create(u);

    // выбираем пулл
    auto& pool = select_pool(u);

    // выбираем подключение
    return pool.get(s);
}

store& store::inst() noexcept
{
    static store i;
    return i;
}

} // namespace capst
