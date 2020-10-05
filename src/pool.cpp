#include "pool.hpp"
#include "conf.hpp"
#include "journal.hpp"

#include "btdef/text.hpp"

#include <memory>

using namespace std::literals;

namespace capst {

std::string pool::create_transaction_id()
{
    return std::to_string(++transaction_seq_) + '@' + name_;
}

void pool::clear() noexcept
{
    try
    {
        lock l(mutex_);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool clear: "sv;
            text += name_;
            text += " ready: "sv;
            text += std::to_string(ready_.size());
            return text;
        });
#endif
        ready_.clear();
    }
    catch (const std::exception& e)
    {
        capst_journal.cerr([&]{
            std::string text;
            text.reserve(64);
            text += "pool clear: "sv;
            text += name_;
            text += " error - "sv;
            text += e.what();
            return text;
        });
    }
    catch (...)
    {
        capst_journal.cerr([&]{
            std::string text;
            text += "pool destroy: "sv;
            text += name_;
            text += " error"sv;
            return text;
        });
    }
}

pool::pool()
{
    name_ += btdef::to_hex(reinterpret_cast<std::uint64_t>(this));
}

pool::pool(const std::string& name)
    : name_(name)
{   }

connection& pool::get(const settings& conf)
{
    auto max_pool_sockets = conf::max_pool_sockets();

    lock l(mutex_);

    auto i = ready_.begin();
    if (i != ready_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " using an existing connection, ready: "sv;
            text += std::to_string(ready_.size());
            text += " active: "sv;
            text += std::to_string(active_.size());
            return text;
        });
#endif
        active_.splice(active_.begin(), ready_, i);
    }
    else
    {
        if (active_.size() >= max_pool_sockets)
        {
            throw std::runtime_error("pool: max pool sockets=" +
                                     std::to_string(max_pool_sockets));
        }
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " create connection, active: "sv;
            text += std::to_string(active_.size());
            text += ", max="sv;
            text += std::to_string(max_pool_sockets);
            return text;
        });
#endif // CAPSTOMP_TRACE_LOG
        active_.emplace_front(*this);
    }

    // получаем соединение
    auto& conn = active_.front();

    conn.set_state(1);

    // получаем указатель на соединение
    auto connection_id = active_.begin();
    // выставляем указатель
    conn.set(connection_id);

    // сбрасываем параметры
    // передаем конфиг
    conn.init(conf);

    return conn;
}

void pool::release_connection(connection_id_type connection_id)
{
    auto pool_sockets = conf::pool_sockets();

    if (connection_id->good() && (ready_.size() < pool_sockets))
    {
        connection_id->set_state(11);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " ready: "sv;
            text += std::to_string(ready_.size());
            text += " active: "sv;
            text += std::to_string(active_.size());
            text += " store connection"sv;
            auto id = connection_id->transaction_id();
            if (!id.empty())
            {
                text += ": transaction:"sv;
                text += connection_id->transaction_id();
            }
            return text;
        });
#endif

        // перемещаем соединенеи в список готовых к работе
        ready_.splice(ready_.begin(), active_, connection_id);
        auto& conn = ready_.front();
        conn.set(ready_.begin());
    }
    else
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " ready: "sv;
            text += std::to_string(ready_.size());
            text += " active: "sv;
            text += std::to_string(active_.size());
            text += " erase connection"sv;
            auto id = connection_id->transaction_id();
            if (!id.empty())
            {
                text += ": transaction:"sv;
                text += connection_id->transaction_id();
            }
            return text;
        });
#endif

        active_.erase(connection_id);
    }
}

void pool::release(connection_id_type connection_id)
{
    lock l(mutex_);

    release_connection(connection_id);
}

pool::transaction_id_type pool::create_transaction(connection_id_type connection_id)
{
    lock l(mutex_);
    // создаем транзакцию
    return transaction_store_.emplace(
        transaction_store_.end(), create_transaction_id(), connection_id);
}

// подтверждаем свою операцию и возвращаем список коммитов
pool::transaction_store_type pool::get_uncommited(transaction_id_type i)
{
    transaction_store_type rc;

    lock l(mutex_);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " transaction:"sv;
            text += i->id();
            text += " ready"sv;
            return text;
        });
#endif

    // в любом случае наша транзакция выполнена
    // это должно вызываться внутри мутекса
    i->set(true);

    // проверяем, является ли
    // наша транзакция первой в списке
    // если она не первая
    // значит есть та, которая еще выполняется
    auto b = transaction_store_.begin();
    if (i != b)
    {
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " transaction:"sv;
            text += i->id();
            text += " deffered commit: "sv;
            text += std::to_string(std::distance(b, i));
            for (auto d = b; d != i; ++d)
            {
                text += ' ';
                text += "transaction:"sv;
                text += d->id();
            }
            return text;
        });

        // если наша транзакция не первая в списке
        // просто говорим что мы законичли работу
        // коммитить нашу транзакцию будет другой поток
        // возвращаем пустой лист транзакций
        return rc;
    }

    // иначе :)
    // формируем список коммитов проходя по очереди до первого не готовго
    // смотрим сколько было в ожидании
    auto e = transaction_store_.end();
    // пока не достигли конца списка

    // прокручиваем лист
    while (i->ready() && (i != e))
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.trace([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " transaction:"sv;
            text += i->id();
            text += " add commit"sv;
            return text;
        });
#endif
        ++i;
    }

    // формируем список транзакций для отправки коммитов
    rc.splice(rc.begin(), transaction_store_, b, i);

#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.trace([&]{
        std::string text;
        text.reserve(64);
        text += "pool: "sv;
        text += name_;
        text += " transaction store size="sv;
        text += std::to_string(transaction_store_.size());
        return text;
    });
#endif

    // и возвращаем его
    return rc;
}

std::size_t pool::force_commit()
{
    std::size_t rc = 0;

    lock l(mutex_);

    auto i = transaction_store_.begin();
    while (i != transaction_store_.end())
    {
        if (i->ready())
        {
            auto &conn = *i->connection();
            conn.force_commit();
            // возможно это уничтожит этот объект
            // дальше им пользоваться уже нельзя
            release_connection(i->connection());

            i = transaction_store_.erase(i);

            ++rc;
        }
        else
        {
            capst_journal.cout([&]{
                std::string text;
                text.reserve(64);
                text += "pool: "sv;
                text += name_;
                text += " force_commit transaction:"sv;
                text += i->id();
                text += " - not ready, "sv;
                text += " state: "sv;
                text += std::to_string(i->connection()->state());
                return text;
            });

            ++i;
        }
    }

    return rc;
}

std::string pool::json()
{
    std::string rc;
    rc.reserve(64);

    lock l(mutex_);

    rc += "{"sv;
        rc += "\"name\":\""sv; rc += name_; rc += "\""sv; rc += ',';
        rc += "\"ready\":"sv; rc += json_arr(ready_); rc += ',';
        rc += "\"active\":"sv; rc += json_arr(active_);
    rc += "}"sv;

    return rc;
}

std::string pool::json_arr(list_type& list)
{
    std::string rc;
    std::string tmp;
    tmp.reserve(2048);
    rc += '[';
    for (auto& c: list)
    {
        if (!tmp.empty())
            tmp += ',';

        tmp += '{';
        tmp += "\"socket\":"sv;
        tmp += std::to_string(c.socket().fd());
        tmp += ",\"state\":"sv;
        tmp += std::to_string(c.state());
        tmp += ",\"total_count\":"sv;
        tmp += std::to_string(c.total_count());
        tmp += '}';
    }
    rc += tmp;
    rc += ']';

    return rc;
}

} // namespace capst
