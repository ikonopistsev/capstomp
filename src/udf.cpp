#include "store.hpp"
#include "journal.hpp"
#include "mysql.hpp"
#include <thread>
#include <sys/types.h>

#ifndef WIN32
#include <unistd.h>
#endif // WIN32

#include "stompconn/version.hpp"
#include "stomptalk/version.hpp"

//#define CAPSTOMP_STAPPE_TEST
//#define CAPSTOMP_THROW_TEST

using namespace std::literals;

// журнал работы
capst::journal capst_journal;

struct version
{
    version() noexcept
    {
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
        constexpr static std::string_view capst_version =
            STR(CAPSTOMP_PLUGIN_VERSION);
        constexpr static std::string_view capst_cxx_name =
            STR(CAPSTOMP_CXX_NAME);
#undef STR_HELPER
#undef STR

        capst_journal.cout([&]{
            std::string text;
            text += 'v';
            text += capst_version;
            if (!capst_cxx_name.empty())
            {
                text += " ("sv;
                text += capst_cxx_name;
                text += ")"sv;
            }
            text += ", stompconn v"sv;
            text += stompconn::version();
            text += ", stomptalk v"sv;
            text += stomptalk::version();
            text += ", libevent v"sv;
            text += event_get_version();
#ifndef WIN32
            text += ", mysqld pid="sv;
            text += std::to_string(getpid());
#endif // WIN32
            return text;
        });
    }
};

static const version capst_version_startup;

//             0        1                2             3
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
extern "C" my_bool capstomp_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    capst::connection *conn = nullptr;

    try
    {
        auto args_count = args->arg_count;
        if ((args_count < 3) ||
            (!((args->arg_type[0] == STRING_RESULT) &&
               (args->arg_type[1] == STRING_RESULT) &&
               (args->arg_type[2] == STRING_RESULT))))
        {
            strncpy(msg, "bad args type, use "
                "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])",
                MYSQL_ERRMSG_SIZE);
            return 1;
        }

        std::string u(args->args[0], args->lengths[0]);
        if (u.empty())
        {
            strncpy(msg, "empty uri, use "
                "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])",
                MYSQL_ERRMSG_SIZE);
            return 1;
        }

        // парсим урл
        btpro::uri uri(u);
        // получаем хранилище
        auto& store = capst::store::inst();

        // получаем пулл соединенией
        conn = &store.get(uri);

        // сохраняем
        initid->ptr = reinterpret_cast<char*>(conn);

        // подключаемся либо повтороно используем соединение
        conn->connect(uri);

        initid->maybe_null = 0;
        initid->const_item = 0;

        return 0;
    }
    catch (const std::exception& e)
    {
        snprintf(msg, MYSQL_ERRMSG_SIZE, "%s", e.what());
    }
    catch (...)
    {
        strncpy(msg, ":*(", MYSQL_ERRMSG_SIZE);
    }

    if (conn)
    {
        // закроем сокет, чтобы пометить коннект как не удачный
        conn->close();
        
        // но не бдуем его отдавать в пул
        if (conn->with_no_error())
            return 0;

        // коммитим все зависящие от нас транзакции
        // и возвращаем соединение в пулл
        conn->commit();
    }

    return 1;
}

template<class T>
bool detect(std::string_view key) noexcept
{
    constexpr auto text = T::text;
    constexpr auto text_size = T::text_size;
    return text_size != key.size() ? false
        : std::memcmp(text.data(), key.data(), text_size) == 0;
}

bool capstomp_fill_kv_header(stompconn::send& frame,
                             const char *ptr, const char *end)
{
    bool custom_content_type = false;
    constexpr auto eq = "="sv;

    auto f = std::find_first_of(ptr, end, eq.begin(), eq.end());
    if ((f != end) && (ptr != f))
    {
        std::string_view key(ptr, std::distance(ptr, f++));
        std::string_view val(f, std::distance(f, end));

        if (key.empty() || val.empty())
        {
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "invalid header"sv;
                if (key.size() < 32)
                {
                    text += ": "sv;
                    text += key;
                }
                return text;
            });
            return custom_content_type;
        }

        using namespace stomptalk;
        using content_type = stomptalk::header::tag::content_type;
        custom_content_type = detect<content_type>(key);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "header+ "sv;
            text += key;
            text += ": "sv;
            text += val;
            return text;
        });
#endif

        frame.push(header::make(key, val));
    }
    else
    {
        capst_journal.trace([&]{
            std::string text;
            text += "bad header"sv;
            return text;
        });
    }
    return custom_content_type;
}

bool capstomp_split_kv_header(stompconn::send& frame,
                              const char *ptr, const char *end)
{
    bool custom_content_type = false;
    constexpr static std::string_view a = "&"sv;

    for (auto curr = ptr; curr != end && ptr != end; ptr = curr + 1)
    {
        curr = std::find_first_of(ptr, end, a.begin(), a.end());
        if (ptr != curr)
            custom_content_type |= capstomp_fill_kv_header(frame, ptr, curr);
    }

    return custom_content_type;
}

//             0        1                2             3
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
bool capstomp_fill_headers(stompconn::send& frame,
                           UDF_ARGS* args, unsigned int from)
{
    bool custom_content_type = false;
    auto arg_count = args->arg_count;
#ifdef CAPSTOMP_TRACE_LOG
    if (from < arg_count)
    {

        capst_journal.trace([=]{
            std::string text;
            text += "connection: fill headers from="sv;
            text += std::to_string(from);
            text += " arg_count="sv;
            text += std::to_string(arg_count);
            return text;
        });
    }
#endif
    for (unsigned int i = from; i < arg_count; ++i)
    {
        if (args->arg_type[i] == STRING_RESULT)
        {
            auto val_ptr = args->args[i];
            auto val_size = args->lengths[i];
            if (val_ptr && val_size)
            {
                auto rc = capstomp_split_kv_header(frame,
                    val_ptr, val_ptr + val_size);
                if (rc)
                    custom_content_type = true;
            }
        }
    }

    return custom_content_type;
}

//             0        1                2             3
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
long long capstomp_content(bool json, UDF_INIT* initid, UDF_ARGS* args,
                   char* is_null, char* error)
{
    auto conn = reinterpret_cast<capst::connection*>(initid->ptr);
#ifdef CAPSTOMP_STATE_DEBUG
    conn->set_state(5);
#endif
    try
    {
        // если сокет закрыт
        // значит режим без ошибок
        if (!conn->good() && conn->with_no_error())
        {
            // просто выходим
            *is_null = 0;
            *error = 0;
            return 0;
        }

        std::string destination(conn->destination());
        std::string_view routing_key(args->args[1], args->lengths[1]);
        if (!routing_key.empty())
        {
            destination += '/';
            destination += routing_key;
        }

        stompconn::send frame(destination);
        if (!capstomp_fill_headers(frame, args, 3))
        {
            if (json)
                frame.push(stomptalk::header::content_type_json());
        }

        stompconn::buffer payload;
        payload.append_ref(args->args[2], args->lengths[2]);
        frame.payload(std::move(payload));

#ifdef CAPSTOMP_STAPPE_TEST
        // это для теста медленного триггера
        // подвешиваем на 30 секунд
        static bool stappe = true;
        if (stappe)
        {
            stappe = false;
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
#endif

#ifdef CAPSTOMP_THROW_TEST
        throw std::runtime_error("capstomp throw test");
#endif

        return static_cast<long long>(conn->send_content(std::move(frame)));
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            return std::string(e.what());
        });
    }
    catch (...)
    {
        capst_journal.cerr([&]{
            return ":*(";
        });
    }

    *is_null = 0;
    *error = 1;

    conn->close();

    return 0;
}

extern "C" void capstomp_deinit(UDF_INIT* initid)
{
    try
    {
        auto conn = reinterpret_cast<capst::connection*>(initid->ptr);
#ifdef CAPSTOMP_STATE_DEBUG
        conn->set_state(7);
#endif
        // возможно, это уничтожит этот объект соединения
        conn->commit();
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            return std::string(e.what());
        });
    }
    catch (...)
    {
        capst_journal.cerr([&]{
            return ":*(";
        });
    }
}

extern "C" my_bool capstomp_json_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_init(initid, args, msg);
}

extern "C" long long capstomp(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_content(false, initid, args, is_null, error);
}

extern "C" long long capstomp_json(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_content(true, initid, args, is_null, error);
}

extern "C" void capstomp_json_deinit(UDF_INIT* initid)
{
    capstomp_deinit(initid);
}



