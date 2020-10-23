#include "store.hpp"
#include "journal.hpp"
#include "mysql.hpp"
#include <thread>
#include <sys/types.h>
#include <unistd.h>

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
#undef STR_HELPER
#undef STR

        capst_journal.cout([&]{
            std::string text;
            text += 'v';
            text += capst_version;
            text += ", stompconn v"sv;
            text += stompconn::version();
            text += ", stomptalk v"sv;
            text += stomptalk::version();
            text += ", libevent v"sv;
            text += btpro::queue::version();
            text += ", mysqld pid="sv;
            text += std::to_string(getpid());
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

        // подключаемся либо повтороно используем соединение
        conn->connect(uri);

        initid->maybe_null = 0;
        initid->const_item = 0;

        // сохраняем
        initid->ptr = reinterpret_cast<char*>(conn);

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

        // коммитим все зависящие от нас транзакции
        // и возвращаем соединение в пулл
        conn->commit();
    }

    return 1;
}

//             0        1                2             3
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
// "capstomp_json(\"uri\", \"routing-key\", \"json-data\"[, param])"
bool capstomp_fill_headers(stompconn::send& frame,
                           UDF_ARGS* args, unsigned int from)
{
    using stomptalk::header::detect;
    using content_type = stomptalk::header::tag::content_type;

    bool has_content_type = false;

#ifdef CAPSTOMP_TRACE_LOG
    if (from < args->arg_count) {
        capst_journal.trace([]{
            return std::string("connection: fill headers");
        });
    }
#endif
    for (unsigned int i = from; i < args->arg_count; ++i)
    {
        auto key_ptr = args->attributes[i];
        auto key_size = args->attribute_lengths[i];
        auto val_ptr = args->args[i];
        auto val_size = args->lengths[i];
        if (key_ptr && key_size && val_ptr && val_size)
        {
            // проверяем есть ли кастомный content_type
            std::string_view key(key_ptr, key_size);
            if (detect(content_type(), key))
                has_content_type = true;

            using namespace stomptalk;

            auto type = args->arg_type[i];
            switch (type)
            {
            case REAL_RESULT: {
                frame.push(header::make(std::string_view(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<double*>(val_ptr))));
                break;
            }
            case INT_RESULT: {
                frame.push(header::make(std::string_view(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<long long*>(val_ptr))));
                break;
            }
            case STRING_RESULT:
            case DECIMAL_RESULT: {
                frame.push(header::make(std::string_view(key_ptr, key_size),
                    std::string_view(val_ptr, val_size)));
                break;
            }
            default:;
            }
        }
    }

    return has_content_type;
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
    conn->set_state(5);
    try
    {
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

        btpro::buffer payload;
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

        conn->set_state(7);
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



