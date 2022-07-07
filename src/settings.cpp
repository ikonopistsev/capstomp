#include "settings.hpp"
#include "journal.hpp"
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
            constexpr auto with_receipt = "receipt"sv;
            constexpr auto with_timestamp = "timestamp"sv;
            constexpr auto with_persistent = "persistent"sv;
            constexpr auto with_transaction = "transaction"sv;
            constexpr auto with_no_error = "no_error"sv;
            constexpr auto with_skip_error = "skip_error"sv;
            for (auto h = hdr.tqh_first; h; h = h->next.tqe_next)
            {
                auto key = h->key;
                auto val = h->value;
                if (key && val)
                {
                    if (with_receipt == key)
                    {
                        auto receipt = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set receipt = "sv;
                            text += std::to_string(receipt);
                            return text;
                        });
#endif
                        receipt_ = receipt;
                    }
                    else if (with_timestamp == key)
                    {
                        auto timestamp = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set timestamp = "sv;
                            text += std::to_string(timestamp);
                            return text;
                        });
#endif
                        timestamp_ = timestamp;
                    }
                    else if (with_transaction == key)
                    {
                        auto transaction = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set transaction = "sv;
                            text += std::to_string(transaction);
                            return text;
                        });
#endif
                        transaction_ = transaction;
                    }
                    else if (with_persistent == key)
                    {
                        auto persistent = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set persistent = "sv;
                            text += std::to_string(persistent);
                            return text;
                        });
#endif
                        persistent_ = persistent;
                    }
                    else if (with_no_error == key)
                    {
                        auto no_error = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set no_error = "sv;
                            text += std::to_string(no_error);
                            return text;
                        });
#endif
                        no_error_ = no_error;
                    }           
                    else if (with_skip_error == key)
                    {
                        auto no_error = read_bool(val);
#ifdef CAPSTOMP_TRACE_LOG
                        capst_journal.trace([=]{
                            std::string text;
                            text += "set skip_error = "sv;
                            text += std::to_string(no_error);
                            return text;
                        });
#endif
                        no_error_ = no_error;
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

