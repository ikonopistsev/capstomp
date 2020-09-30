#include "store.hpp"
#include "pool.hpp"
#include "journal.hpp"
#include "mysql.hpp"
#include "conf.hpp"

using namespace std::literals;

namespace capst {

std::string endpoint(const uri& uri, const std::string& fragment)
{
    // смена пароля приведет к формированию нового пула
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

pool& store::select_pool(const uri& u, const std::string& forced_pool)
{
    auto name = endpoint(u, forced_pool);
    auto pool_max = conf::max_pool_count();

    lock l(mutex_);

    auto f = store_.find(name);
    if (f != store_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text += "store: use existing "sv;
            text += f->second.json();
            text += ", size="sv;
            text += std::to_string(store_.size());
            return text;
        });
#endif
        return f->second;
    }

    // проверяем доступно ли создание нового пула
    if (store_.size() > pool_max)
    {
        throw std::runtime_error("store: max pool count =" +
                                 std::to_string(pool_max));
    }

    capst_journal.cout([&]{
        std::string text;
        text.reserve(64);
        text += "store: create pool, size="sv;
        text += std::to_string(store_.size());
        text += ", max="sv;
        text += std::to_string(pool_max);
        return text;
    });

    return store_[name];
}

connection& store::get(const uri& u)
{
    // парсим настройки
    auto conf = settings::create(u);

    // выбираем пулл
    auto& pool = select_pool(u, conf.pool());

    // выбираем подключение
    return pool.get(conf);
}

std::string store::json()
{
    std::string rc;
    rc.reserve(256);

    bool first_line = true;

    lock l(mutex_);

    rc += '[';

    for(auto& i: store_)
    {
        if (!first_line)
            rc += ',';

        rc += '{';
            rc += "\"name\":\""sv; rc += std::get<0>(i); rc += "\","sv;
            rc += "\"pool\":"sv; rc += std::get<1>(i).json();
        rc += '}';

        first_line = false;
    }

    rc += ']';

    return rc;
}

void store::erase(const std::string& name)
{
    lock l(mutex_);

    auto f = store_.find(name);
    if (f == store_.end())
        throw std::runtime_error("store erase: " + name + " - not found");

    auto running = f->second.active_size();
    if (running > 0)
        throw std::runtime_error("store erase: " + name + " - has " +
                                 std::to_string(running) + " running");

    store_.erase(f);
}

void store::clear()
{
    lock l(mutex_);
    store_.clear();
}

store& store::inst() noexcept
{
    static store i;
    return i;
}

} // namespace capst

extern "C" my_bool capstomp_store_erase_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    try
    {
        auto args_count = args->arg_count;
        if ((args_count != 1) ||
            (!(args->arg_type[0] == STRING_RESULT)) || (args->lengths[0] == 0))
        {
            strncpy(msg, "bad args, use capstomp_store_erase(\"pool_name\")",
                MYSQL_ERRMSG_SIZE);
            return 1;
        }

        auto& store = capst::store::inst();
        store.erase(std::string(args->args[0], args->lengths[0]));

        initid->maybe_null = 0;
        initid->const_item = 0;

        return my_bool();
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            return std::string(e.what());
        });
        snprintf(msg, MYSQL_ERRMSG_SIZE, "%s", e.what());
    }
    catch (...)
    {

        strncpy(msg, ":*(", MYSQL_ERRMSG_SIZE);

        capst_journal.cerr([&]{
            return ":*(";
        });
    }

    return 1;
}

extern "C" my_bool capstomp_status_init(UDF_INIT* initid, UDF_ARGS*, char* msg)
{
    try
    {
        initid->maybe_null = 1;
        initid->const_item = 0;

        auto& store = capst::store::inst();
        auto result = store.json();
        auto size = result.size();
        if (size)
        {
            initid->max_length = size;
            initid->ptr = new char[size + 1];
            std::memcpy(initid->ptr, result.data(), size);
            initid->ptr[size] = '\0';
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            std::string text;
            text += "capstomp_status_init: "sv;
            text += e.what();
            return text;
        });

        snprintf(msg, MYSQL_ERRMSG_SIZE, "%s", e.what());
    }
    catch (...)
    {
        static const std::string text = "capstomp_status_init :*(";

        capst_journal.cerr([&]{
            return text;
        });

        strncpy(msg, text.data(), MYSQL_ERRMSG_SIZE);
    }

    return 1;
}

extern "C" char* capstomp_status(UDF_INIT* initid, UDF_ARGS*,
                       char*, unsigned long* length,
                       char* is_null, char* error)
{
    auto ptr = initid->ptr;
    if (ptr)
    {
        *length = std::strlen(ptr);
        return ptr;
    }

    *error = 1;
    *length = 0;
    *is_null = 1;
    return nullptr;
}

extern "C" void capstomp_status_deinit(UDF_INIT* initid)
{
    delete[] initid->ptr;
}


// empty func

extern "C" long long capstomp_store_erase(UDF_INIT*,
    UDF_ARGS*, char*, char*)
{
    return static_cast<long long>(1);
}

extern "C" void capstomp_store_erase_deinit(UDF_INIT*)
{   }
