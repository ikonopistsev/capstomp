#include "conf.hpp"
#include "journal.hpp"
#include <string>
#include <algorithm>
#include "mysql.hpp"
#include <string.h>

namespace capst {

using namespace std::literals;

void conf::set_timeout(std::size_t value) noexcept
{
    value = std::max(value, timeout_min);
    value = std::min(value, timeout_max);
    capst_journal.cout([value]{
        std::string text;
        text += "set timeout = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().timeout_ = value;
}

void conf::set_max_pool_count(std::size_t value) noexcept
{
    value = std::max(value, max_pool_count_min);
    capst_journal.cout([value]{
        std::string text;
        text += "set max pool count = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().max_pool_count_ = value;
}

void conf::set_max_pool_sockets(std::size_t value) noexcept
{
    value = std::max(value, max_pool_sockets_min);
    capst_journal.cout([value]{
        std::string text;
        text += "set max pool sockets = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().max_pool_sockets_ = value;
}

void conf::set_pool_sockets(std::size_t value) noexcept
{
    value = std::max(value, pool_sockets_min);
    capst_journal.cout([value]{
        std::string text;
        text += "set max pool pool_sockets = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().pool_sockets_ = value;
}

void conf::set_request_limit(std::size_t value) noexcept
{
    value = std::max(value, request_limit_min);
    capst_journal.cout([value]{
        std::string text;
        text += "set request limit = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().request_limit_ = value;
}

void conf::set_enable(std::size_t value) noexcept
{
    capst_journal.cout([value]{
        std::string text;
        text += "set enable = "sv;
        text += (value) ? "on"sv : "off"sv;
        return text;
    });

    inst().enable_ = value;
}

conf& conf::inst() noexcept
{
    static capst::conf conf;
    return conf;
}

} // namspace capst

extern "C" my_bool capstomp_timeout_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    auto arg_count = args->arg_count;
    if ((arg_count == 1) && (args->arg_type[0] == INT_RESULT) && args->args[0])
    {
        auto new_timeout = *reinterpret_cast<long long*>(args->args[0]);
        capst::conf::set_timeout(static_cast<std::size_t>(new_timeout));

        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::timeout()));

        return my_bool();
    }
    else if (arg_count == 0)
    {
        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::timeout()));

        return my_bool();
    }

    initid->ptr = nullptr;

    strncpy(msg, "bad args, use capstomp_timeout([timeout_ms])",
        MYSQL_ERRMSG_SIZE);

    return 1;
}

extern "C" long long capstomp_timeout(UDF_INIT* initid,
    UDF_ARGS*, char* is_null, char* error)
{
    auto ptr = initid->ptr;
    if (ptr)
    {
        return static_cast<long long>(
                reinterpret_cast<std::intptr_t>(ptr));
    }

    *error = 1;
    *is_null = 1;
    return 0;
}

extern "C" void capstomp_timeout_deinit(UDF_INIT*)
{   }


extern "C" my_bool capstomp_max_pool_count_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    auto arg_count = args->arg_count;
    if ((arg_count == 1) && (args->arg_type[0] == INT_RESULT) && args->args[0])
    {
        auto new_max_pool_count = *reinterpret_cast<long long*>(args->args[0]);
        capst::conf::set_max_pool_count(static_cast<std::size_t>(new_max_pool_count));

        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::max_pool_count()));

        return my_bool();
    }
    else if (arg_count == 0)
    {
        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::max_pool_count()));

        return my_bool();
    }

    initid->ptr = nullptr;

    strncpy(msg, "bad args, use capstomp_max_pool_count([count])",
        MYSQL_ERRMSG_SIZE);

    return 1;
}

extern "C" long long capstomp_max_pool_count(UDF_INIT* initid,
    UDF_ARGS*, char* is_null, char* error)
{
    auto ptr = initid->ptr;
    if (ptr)
    {
        return static_cast<long long>(
                reinterpret_cast<std::intptr_t>(ptr));
    }

    *error = 1;
    *is_null = 1;
    return 0;
}

extern "C" void capstomp_max_pool_count_deinit(UDF_INIT*)
{   }

extern "C" my_bool capstomp_max_pool_sockets_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    auto arg_count = args->arg_count;
    if ((arg_count == 1) && (args->arg_type[0] == INT_RESULT) && args->args[0])
    {
        auto new_max_pool_sockets = *reinterpret_cast<long long*>(args->args[0]);
        capst::conf::set_max_pool_sockets(static_cast<std::size_t>(new_max_pool_sockets));

        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::max_pool_sockets()));

        return my_bool();
    }
    else if (arg_count == 0)
    {
        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::max_pool_sockets()));

        return my_bool();
    }

    initid->ptr = nullptr;

    strncpy(msg, "bad args, use capstomp_max_pool_sockets([count])",
        MYSQL_ERRMSG_SIZE);

    return 1;
}

extern "C" long long capstomp_max_pool_sockets(UDF_INIT* initid,
    UDF_ARGS*, char* is_null, char* error)
{
    auto ptr = initid->ptr;
    if (ptr)
    {
        return static_cast<long long>(
                reinterpret_cast<std::intptr_t>(ptr));
    }

    *error = 1;
    *is_null = 1;
    return 0;
}

extern "C" void capstomp_max_pool_sockets_deinit(UDF_INIT*)
{   }

extern "C" my_bool capstomp_pool_sockets_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    auto arg_count = args->arg_count;
    if ((arg_count == 1) && (args->arg_type[0] == INT_RESULT) && args->args[0])
    {
        auto new_pool_sockets = *reinterpret_cast<long long*>(args->args[0]);
        capst::conf::set_pool_sockets(static_cast<std::size_t>(new_pool_sockets));

        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::pool_sockets()));

        return my_bool();
    }
    else if (arg_count == 0)
    {
        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::pool_sockets()));

        return my_bool();
    }

    initid->ptr = nullptr;

    strncpy(msg, "bad args, use capstomp_pool_sockets([count])",
        MYSQL_ERRMSG_SIZE);

    return 1;
}

extern "C" long long capstomp_pool_sockets(UDF_INIT* initid,
    UDF_ARGS*, char* is_null, char* error)
{
    auto ptr = initid->ptr;
    if (ptr)
    {
        return static_cast<long long>(
                reinterpret_cast<std::intptr_t>(ptr));
    }

    *error = 1;
    *is_null = 1;
    return 0;
}

extern "C" void capstomp_pool_sockets_deinit(UDF_INIT*)
{   }

extern "C" my_bool capstomp_enable_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    auto arg_count = args->arg_count;
    if ((arg_count == 1) && (args->arg_type[0] == INT_RESULT) && args->args[0])
    {
        auto enable = *reinterpret_cast<long long*>(args->args[0]);
        capst::conf::set_enable(static_cast<std::size_t>(enable));


        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::enable() ? 1 : 0));

        return my_bool();
    }
    else if (arg_count == 0)
    {
        initid->ptr =
            reinterpret_cast<char*>(static_cast<std::intptr_t>(
                capst::conf::enable() ? 1 : 0));

        return my_bool();
    }

    initid->ptr = nullptr;

    strncpy(msg, "bad args, use capstomp_enable([enable_flag])",
        MYSQL_ERRMSG_SIZE);

    return 1;
}

extern "C" long long capstomp_enable(UDF_INIT* initid,
    UDF_ARGS*, char* is_null, char*)
{
    *is_null = 0;

    auto ptr = initid->ptr;
    if (initid->ptr)
    {
        return static_cast<long long>(
                reinterpret_cast<std::intptr_t>(ptr));
    }

    return 0;
}

extern "C" void capstomp_enable_deinit(UDF_INIT*)
{   }
