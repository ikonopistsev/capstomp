#include "pool.hpp"
#include "journal.hpp"

#include "btdef/text.hpp"

#include <memory>

using namespace std::literals;

namespace capst {

std::string pool::create_transaction_id()
{
    return std::to_string(++sequence_) + '@' + name_;
}

void pool::destroy() noexcept
{
    try
    {
        std::unique_lock<std::mutex> l(mutex_);
        while (!active_.empty())
            cv_.wait(l);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool destroy: "sv;
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
            text += "pool destroy: "sv;
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

pool::~pool() noexcept
{
    destroy();
}

connection& pool::get(const settings& conf)
{
    static constexpr auto max_pool_socket =
        std::size_t{CAPSTOMP_MAX_POOL_SOCKETS};

    lock l(mutex_);

    auto i = ready_.begin();
    if (i != ready_.end())
    {
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
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
        if (active_.size() >= max_pool_socket)
            throw std::runtime_error("pool: max_pool_socket=" +
                                     std::to_string(max_pool_socket));

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " create new connection, active: "sv;
            text += std::to_string(active_.size());
            return text;
        });
#endif
        active_.emplace_front(*this);
    }

    // получаем соединение
    auto& conn = active_.front();
    // получаем указатель на соединение
    auto connection_id = active_.begin();
    // выставляем указатель
    conn.set(connection_id);

    // сбрасываем параметры
    conn.init();
    // передаем конфиг
    conn.set(conf);

    if (conf.transaction())
    {
        // создаем транзакцию
        auto transaction_id = transaction_store_.emplace(
            transaction_store_.end(), create_transaction_id(), connection_id);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " set connection deferred transaction:"sv;
            text += transaction_id->id();
            return text;
        });
#endif
        // сохраняем транзакцию в соединении
        conn.set(transaction_id);
    }

    return conn;
}

// подтверждаем свою операцию и возвращаем список коммитов
pool::transaction_store_type pool::get_uncommited(transaction_id_type i)
{
    transaction_store_type rc;

    lock l(mutex_);

#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
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
#ifdef CAPSTOMP_TRACE_LOG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: "sv;
            text += name_;
            text += " transaction:"sv;
            text += i->id();
            text += " deffered commit"sv;
            return text;
        });
#endif

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
        capst_journal.cout([&]{
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
    capst_journal.cout([&]{
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


void pool::release(connection_id_type connection_id)
{
    static constexpr auto pool_socket = std::size_t{CAPSTOMP_POOL_SOCKETS};

    lock l(mutex_);

#ifdef CAPSTOMP_TRACE_LOG
    capst_journal.cout([&]{
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

    if (ready_.size() < pool_socket)
    {
        // перемещаем соединенеи в список готовых к работе
        ready_.splice(ready_.begin(), active_, connection_id);
        auto& conn = ready_.front();
        conn.set(ready_.begin());
    }
    else
        active_.erase(connection_id);

    if (active_.empty())
        cv_.notify_one();
}

std::string pool::str()
{
    std::string rc;
    lock l(mutex_);
    rc += "pool: "sv;
    rc += name_;
    rc += " ready: "sv;
    rc += std::to_string(ready_.size());
    rc += " active: "sv;
    rc += std::to_string(active_.size());
    return rc;
}

} // namespace capst
