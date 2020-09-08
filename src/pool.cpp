#include "pool.hpp"
#include "journal.hpp"
#include <memory>
#include "btdef/text.hpp"

using namespace std::literals;

namespace capst {

std::string pool::create_transaction_id()
{
    return std::to_string(++sequence_) + '@' + name_;
}

pool::pool()
{
    name_ += btdef::to_hex(reinterpret_cast<std::uint64_t>(this));
}

connection& pool::get()
{
    lock l(mutex_);

    auto i = ready_.begin();
    if (i != ready_.end())
    {
#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: using an existing connection, ready: "sv;
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
#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: create new connection, "sv;
            text += " active: "sv;
            text += std::to_string(active_.size());
            return text;
        });
#endif
        active_.emplace_front(*this);
    }

    auto& conn = active_.front();
    auto connection_id = active_.begin();

    conn.set(connection_id);

    // создаем транзакцию
    auto transaction_id = transaction_store_.emplace(
        transaction_store_.end(), create_transaction_id(), connection_id);

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: set connection transaction: "sv;
            text += transaction_id->id();
            return text;
        });
#endif

    // сохраняем транзакциб в соединении
    conn.set(transaction_id);

    return conn;
}

// подтверждаем свою операцию и возвращаем список коммитов
pool::transaction_store_type
pool::get_uncommited(transaction_id_type i)
{
    transaction_store_type rc;

    lock l(mutex_);

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: transaction_id="sv;
            text += i->id();
            text += " ready"sv;
            return text;
        });
#endif

    // в любом случае наша транзакция выполнена
    i->set(true);

    // проверяем, является ли
    // наша транзакция первой в списке
    // если она не первая
    // значит есть та, которая еще выполняется
    auto b = transaction_store_.begin();
    if (i != b)
    {
#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: transaction_id="sv;
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
#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: transaction_id="sv;
            text += i->id();
            text += " add commit"sv;
            return text;
        });
#endif
        ++i;
    }

    // формируем список транзакций для отправки коммитов
    rc.splice(rc.begin(), transaction_store_, b, i);

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: transaction store size="sv;
            text += std::to_string(transaction_store_.size());
            return text;
        });
#endif

    // и возвращаем его
    return rc;
}


void pool::release(connection_id_type connection_id)
{
    lock l(mutex_);

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text;
            text.reserve(64);
            text += "pool: ready: "sv;
            text += std::to_string(ready_.size());
            text += " active: "sv;
            text += std::to_string(active_.size());
            text += " store connection: transaction_id=";
            text += connection_id->transaction_id();
            return text;
        });
#endif

    // перемещаем соединенеи в список готовых к работе
    ready_.splice(ready_.begin(), active_, connection_id);
    auto& conn = ready_.front();
    conn.set(ready_.begin());
}

} // namespace capst
