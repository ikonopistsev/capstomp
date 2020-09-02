#include "capstomp.hpp"
#include "journal.hpp"
#include <array>
#include "mysql.hpp"

// журнал работы
static const capstomp::journal j;
static std::array<capstomp::connection, CAPSTOMP_SOCKET_SLOTS> connarr;

my_bool capstomp_json_init(capstomp::connection& conn, UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    try
    {
        auto args_count = args->arg_count;
        if ((args_count < 6) ||
            (!(((args->arg_type[0] == INT_RESULT) ||
                (args->arg_type[0] == STRING_RESULT)) &&
               (args->arg_type[1] == STRING_RESULT) &&
               (args->arg_type[2] == STRING_RESULT) &&
               (args->arg_type[3] == STRING_RESULT) &&
               (args->arg_type[4] == STRING_RESULT) &&
               (args->arg_type[5] == STRING_RESULT))))
        {
            strncpy(msg, "bad args type, use "
                         "capstomp_json(port|addr:port, user, passcode, "
                         "vhost, dest, json-data[, param])",
                    MYSQL_ERRMSG_SIZE);
            return 1;
        }

        auto addr_val = args->args[0];
        auto addr_size = args->lengths[0];

        std::string_view user(args->args[1], args->lengths[1]);
        std::string_view passcode(args->args[2], args->lengths[2]);
        std::string_view vhost(args->args[3], args->lengths[3]);

        if (!(addr_val && addr_size))
        {
            strncpy(msg, "bad args, use "
                         "capstomp_json(port|addr:port, user, passcode, "
                         "vhost, dest, json-data[, param])",
                    MYSQL_ERRMSG_SIZE);
            return 1;
        }

        // парсим адрес
        btpro::sock_addr addr;
        auto addr_type = args->arg_type[0];
        if (addr_type == INT_RESULT)
        {
            auto p = static_cast<int>(*reinterpret_cast<long long*>(addr_val));
            addr.assign(btpro::ipv4::loopback(p));
        }
        else
            addr.assign(std::string(addr_val, addr_size));

        conn.connect(capstomp::destination(user, passcode, vhost, addr));
        initid->maybe_null = 0;
        initid->const_item = 0;

        // сохраняем сокет
        initid->ptr = nullptr;

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

    conn.close();
    conn.unlock();

    return 1;
}

bool capstomp_push_header(stompconn::send& frame, UDF_ARGS* args)
{
    using stomptalk::header::detect;

    bool content_type = false;
    for (int i = 6; i < args->arg_count; ++i)
    {
        auto key_ptr = args->attributes[i];
        auto key_size = args->attribute_lengths[i];
        auto val_ptr = args->args[i];
        auto val_size = args->lengths[i];
        if (key_ptr && key_size && val_ptr && val_size)
        {
            // проверяем есть ли кастомный content_type
            std::string_view key(key_ptr, key_size);
            if (detect(stomptalk::header::tag::content_type(), key))
                content_type = true;

            auto type = args->arg_type[i];
            switch (type)
            {
            case REAL_RESULT: {
                stomptalk::header::custom hdr(std::string(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<double*>(val_ptr)));
                frame.push(hdr);
                break;
            }
            case INT_RESULT: {
                stomptalk::header::custom hdr(std::string(key_ptr, key_size),
                    std::to_string(*reinterpret_cast<long long*>(val_ptr)));
                frame.push(hdr);
                break;
            }
            case STRING_RESULT:
            case DECIMAL_RESULT: {
                std::string_view key(key_ptr, key_size);
                std::string_view val(val_ptr, val_size);
                stomptalk::header::fixed hdr(key, val);
                frame.push(hdr);
                break;
            }
            default:;
            }
        }
    }

    return content_type;
}

// "capstomp_json(port|addr:port, user, passcode, vhost, dest, json-data[, param])",
long long capstomp_json(capstomp::connection& conn, UDF_INIT*,
    UDF_ARGS* args, char* is_null, char* error)
{
    try
    {
        std::string_view destination(args->args[4], args->lengths[4]);
        stompconn::send frame(destination);
        if (!capstomp_push_header(frame, args))
            frame.push(stomptalk::header::content_type_json());
        btpro::buffer payload;
        payload.append_ref(args->args[5], args->lengths[5]);
        frame.payload(std::move(payload));
        return static_cast<long long>(conn.send(std::move(frame)));
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

void capstomp_json_deinit(capstomp::connection& conn, UDF_INIT*)
{
    try
    {
        conn.unlock();
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

extern "C" my_bool capstomp_json01_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[0], initid, args, msg);
}

extern "C" long long capstomp_json01(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[0], initid, args, is_null, error);
}

extern "C" void capstomp_json01_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[0], initid);
}

extern "C" my_bool capstomp_json02_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[1], initid, args, msg);
}

extern "C" long long capstomp_json02(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[1], initid, args, is_null, error);
}

extern "C" void capstomp_json02_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[1], initid);
}

extern "C" my_bool capstomp_json03_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[2], initid, args, msg);
}

extern "C" long long capstomp_json03(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[2], initid, args, is_null, error);
}

extern "C" void capstomp_json03_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[2], initid);
}

extern "C" my_bool capstomp_json04_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[3], initid, args, msg);
}

extern "C" long long capstomp_json04(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[3], initid, args, is_null, error);
}

extern "C" void capstomp_json04_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[3], initid);
}

extern "C" my_bool capstomp_json05_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[4], initid, args, msg);
}

extern "C" long long capstomp_json05(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[4], initid, args, is_null, error);
}

extern "C" void capstomp_json05_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[4], initid);
}

extern "C" my_bool capstomp_json06_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[5], initid, args, msg);
}

extern "C" long long capstomp_json06(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[5], initid, args, is_null, error);
}

extern "C" void capstomp_json06_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[5], initid);
}

extern "C" my_bool capstomp_json07_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[6], initid, args, msg);
}

extern "C" long long capstomp_json07(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[6], initid, args, is_null, error);
}

extern "C" void capstomp_json07_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[6], initid);
}

extern "C" my_bool capstomp_json08_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[7], initid, args, msg);
}

extern "C" long long capstomp_json08(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[7], initid, args, is_null, error);
}

extern "C" void capstomp_json08_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[7], initid);
}

extern "C" my_bool capstomp_json09_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[8], initid, args, msg);
}

extern "C" long long capstomp_json09(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[8], initid, args, is_null, error);
}

extern "C" void capstomp_json09_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[8], initid);
}

extern "C" my_bool capstomp_json10_init(UDF_INIT* initid,
    UDF_ARGS* args, char* msg)
{
    return capstomp_json_init(connarr[9], initid, args, msg);
}

extern "C" long long capstomp_json10(UDF_INIT* initid,
    UDF_ARGS* args, char* is_null, char* error)
{
    return capstomp_json(connarr[9], initid, args, is_null, error);
}

extern "C" void capstomp_json10_deinit(UDF_INIT* initid)
{
    return capstomp_json_deinit(connarr[9], initid);
}
