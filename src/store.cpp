#include "store.hpp"
#include "pool.hpp"
#include "journal.hpp"

using namespace std::literals;

namespace capst {

std::string endpoint(const uri& uri, const std::string& fragment)
{
    auto u = uri.user();
    auto a = uri.addr();
    auto p = uri.path();
    auto f = fragment.empty() ? uri.fragment() : fragment;

    std::string t;
    t.reserve(u.size() + a.size() + p.size() + f.size() + 2);

    t += u;
    t += '@';
    t += a;
    t += p;
    t += '#';
    t += f;
    return t;
}

pool& store::select_pool(const uri& u, const settings& conf)
{
    static constexpr auto pool_max = std::size_t{CAPSTOMP_MAX_POOL_COUNT};

    auto name = endpoint(u, conf.pool());

    lock l(mutex_);

    auto f = store_.find(name);
    if (f != store_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text = "store: use existing pool:";
            text += f->second.name();
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

    return store_[name];
}

connection& store::get(const uri& u)
{
    // парсим настройки
    auto conf = settings::create(u);

    // выбираем пулл
    auto& pool = select_pool(u, conf);

    // выбираем подключение
    return pool.get(conf);
}

store& store::inst() noexcept
{
    static store i;
    return i;
}

} // namespace capst
