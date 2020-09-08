#include "store.hpp"
#include "pool.hpp"
#include "journal.hpp"

namespace capst {

pool& store::get(const std::string& uri)
{
    lock l(mutex_);

    auto f = index_.find(uri);
    if (f != index_.end())
    {
#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text = "store: get existing pool";
            return text;
        });
#endif
        return *f->second;
    }

    store_.emplace_front();
    index_[uri] = store_.begin();

#ifndef NDEBUG
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
