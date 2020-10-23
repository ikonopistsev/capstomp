#include "settings.hpp"
#include <event2/keyvalq_struct.h>

using namespace capst;
using namespace std::literals;

bool read_bool(const char *value) noexcept
{
    auto value_true = "true"sv;
    auto value_false = "false"sv;

    if (value_true == value)
        return true;
    if (value_false == value)
        return false;

    return std::atoi(value) > 0;
}

void settings::parse(std::string_view query)
{
    if (!query.empty())
    {
        struct evkeyvalq hdr = {};
        if (0 == evhttp_parse_query_str(query.data(), &hdr))
        {
            auto with_receipt = "receipt"sv;
            auto with_timestamp = "timestamp"sv;
            auto with_persistent = "persistent"sv;
            auto with_transaction = "transaction"sv;
            for (auto h = hdr.tqh_first; h; h = h->next.tqe_next)
            {
                auto key = h->key;
                auto val = h->value;
                if (key && val)
                {
                    if (with_receipt == key)
                    {
                        receipt_ = read_bool(val);
                    }
                    else if (with_timestamp == key)
                    {
                        timestamp_ = read_bool(val);
                    }
                    else if (with_transaction == key)
                    {
                        transaction_ = read_bool(val);
                    }
                    else if (with_persistent == key)
                    {
                        persistent_ = read_bool(val);
                    }
                }
            }
            evhttp_clear_headers(&hdr);
        }
    }
}

settings settings::create(const btpro::uri& u)
{
    settings s;
    s.parse(u.query());
    return s;
}

