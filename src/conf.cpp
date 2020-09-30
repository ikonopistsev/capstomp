#include "conf.hpp"
#include "journal.hpp"
#include <string>
#include <algorithm>
#include "mysql.hpp"

namespace capst {

using namespace std::literals;

void conf::set_read_timeout(std::size_t value) noexcept
{
    value = std::max(value, read_timeout_min);
    capst_journal.cout([value]{
        std::string text;
        text += "set read timeout = "sv;
        text += std::to_string(value);
        return text;
    });

    inst().read_timeout_ = value;
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

extern "C" my_bool capstomp_connection_timeout_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return my_bool();
}

extern "C" long long capstomp_connection_timeout(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return static_cast<long long>(1);
}

extern "C" void capstomp_connection_timeout_deinit(UDF_INIT* initid)
{   }

//capstomp_connection_timeout(1000);
//capstomp_max_pool_count(1000);
//capstomp_max_pool_sockets(1000);
//capstomp_pool_sockets(1000);
