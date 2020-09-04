#include "capstomp.hpp"
#include "journal.hpp"
#include <array>
#include <thread>
#include "mysql.hpp"

// журнал работы
static const cs::journal j;

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

struct version
{
    version() noexcept
    {
        j.cout([]{
            auto text = std::mkstr(std::cref("ver: "));
            text += STR(CAPSTOMP_PLUGIN_VERSION);
            text += stomptalk::sv(" rev: ");
            text += STR(CAPSTOMP_PLUGIN_REVISION);
            return text;
        });
    }
};

#undef STR_HELPER
#undef STR

static const version v;

static cs::connection* get(std::size_t i)
{
    using pool_type = std::array<cs::connection, CAPSTOMP_SOCKET_SLOTS>;
    static pool_type pool;
    return &pool.at(i);
}

//            0          1     2       3           4
// "capstomp("location", dest, socket, json-data[, param])"
// "capstomp("location", dest, json-data[, param])"
// "capstomp_json("location", dest, json-data[, param])"
// "capstomp_json("location", dest, socket, json-data[, param])"
extern "C" my_bool capstomp_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    cs::connection *conn = nullptr;

    try
    {
        auto args_count = args->arg_count;
        if ((args_count < 3) ||
            (!((args->arg_type[0] == STRING_RESULT) &&
               (args->arg_type[1] == STRING_RESULT))))
        {
            strncpy(msg, "bad args type, use "
                "capstomp(\"uri\", \"dest\", \"json-data\"[, param])",
                MYSQL_ERRMSG_SIZE);
            return 1;
        }

        std::size_t num = 0;
        if (args->arg_type[2] == INT_RESULT)
        {
            if ((args_count < 4) || (!(args->arg_type[3] == STRING_RESULT)))
            {
                strncpy(msg, "bad socket type, use "
                        "capstomp(\"uri\", \"dest\", socket, \"json-data\"[, param])",
                        MYSQL_ERRMSG_SIZE);
                return 1;
            }

            auto sock_ptr = args->args[2];
            num = static_cast<std::size_t>(
                *reinterpret_cast<long long*>(sock_ptr));
        }
        else if (args->arg_type[2] != STRING_RESULT)
        {
            strncpy(msg, "bad json-data type, use "
                    "capstomp(\"uri\", \"dest\", \"json-data\"[, param])",
                    MYSQL_ERRMSG_SIZE);
            return 1;
        }
        else
        {
            std::hash<std::thread::id> hf;
            num = hf(std::this_thread::get_id()) % CAPSTOMP_SOCKET_SLOTS;
        }

#ifdef TRACE_SOCKET_NUM
        j.cout([&]{
            std::string text("socket num: ");
            text += std::to_string(num);
            text += stomptalk::sv(" of: ");
            text += std::to_string(CAPSTOMP_SOCKET_SLOTS);
            return text;
        });
#endif

        std::string_view uri(args->args[0], args->lengths[0]);
        if (uri.empty())
        {
            strncpy(msg, "empty uri, use "
                "capstomp(\"uri\", \"dest\", \"json-data\"[, param])",
                MYSQL_ERRMSG_SIZE);
            return 1;
        }

        // получаем соединение
        conn = get(num);
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
        strncpy(msg, "capstomp_json :*(", MYSQL_ERRMSG_SIZE);
    }

    if (conn)
    {
        conn->close();
        conn->unlock();
    }

    return 1;
}

//            0          1     2       3           4
// "capstomp("location", dest, socket, json-data[, param])"
// "capstomp("location", dest, json-data[, param])"
bool capstomp_fill_headers(stompconn::send& frame,
                           UDF_ARGS* args, unsigned int from)
{
    using stomptalk::header::detect;
    using content_type = stomptalk::header::tag::content_type;

    bool has_content_type = false;
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

            using namespace stomptalk::header;
            auto type = args->arg_type[i];
            switch (type)
            {
            case REAL_RESULT: {
                base<std::string_view, std::string> hdr(
                    std::string_view(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<double*>(val_ptr)));
                frame.push(hdr);
                break;
            }
            case INT_RESULT: {
                base<std::string_view, std::string> hdr(
                    std::string_view(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<long long*>(val_ptr)));
                frame.push(hdr);
                break;
            }
            case STRING_RESULT:
            case DECIMAL_RESULT: {
                std::string_view key(key_ptr, key_size);
                std::string_view val(val_ptr, val_size);
                base_ref hdr(key, val);
                frame.push(hdr);
                break;
            }
            default:;
            }
        }
    }

    return has_content_type;
}

//            0          1     2       3           4
// "capstomp("location", dest, socket, json-data[, param])"
// "capstomp("location", dest, json-data[, param])"
long long capstomp_content(bool json, UDF_INIT* initid, UDF_ARGS* args,
                   char* is_null, char* error)
{
    try
    {
        std::string_view destination(args->args[1], args->lengths[1]);
        if (destination.empty())
        {
            j.cerr([&]{
                return std::mkstr(std::cref("destination empty"));
            });

            *is_null = 0;
            *error = 1;

            return 0;
        }

        // определяем позицию контента
        unsigned int j = 2;
        if (args->arg_type[2] == INT_RESULT)
            j = 3;

        stompconn::send frame(destination);
        if (!capstomp_fill_headers(frame, args, j + 1))
        {
            if (json)
                frame.push(stomptalk::header::content_type_json());
        }

        btpro::buffer payload;
        payload.append_ref(args->args[j], args->lengths[j]);
        frame.payload(std::move(payload));

        auto conn = reinterpret_cast<cs::connection*>(initid->ptr);
        return static_cast<long long>(conn->send(std::move(frame)));
    }
    catch (const std::exception& e)
    {
        j.cerr([&]{
            auto text = std::mkstr(std::cref("capstomp_json: "), 320);
            text += e.what();
            return text;
        });
    }
    catch (...)
    {
        j.cerr([&]{
            return "capstomp_json :*(";
        });
    }

    *is_null = 0;
    *error = 1;

    return 0;
}

extern "C" void capstomp_deinit(UDF_INIT* initid)
{
    try
    {
        auto conn = reinterpret_cast<cs::connection*>(initid->ptr);
        conn->unlock();
    }
    catch (const std::exception& e)
    {
        j.cerr([&]{
            auto text = std::mkstr(std::cref("capstomp_json: "), 320);
            text += e.what();
            return text;
        });
    }
    catch (...)
    {
        j.cerr([&]{
            return "capstomp_json :*(";
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
