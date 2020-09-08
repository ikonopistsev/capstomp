#include "pool.hpp"
#include "journal.hpp"
#include <memory>
#include "btdef/text.hpp"

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
            std::string text = "pool: using an existing connection, ready: ";
            text += std::to_string(ready_.size());
            text += " active: ";
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
            std::string text = "pool: create new connection, ";
            text += " active: ";
            text += std::to_string(active_.size());
            return text;
        });
#endif
        active_.emplace_front(*this);
    }

    auto& conn = active_.front();
    conn.set(active_.begin());

    // создаем транзакцию
    auto transaction_id = transaction_store_.emplace(
        transaction_store_.end(), create_transaction_id());

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text = "pool: set connection transaction: ";
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
            std::string text = "pool: set ready transaction_id=";
            text += i->id();
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
            std::string text = "pool: transaction_id=";
            text += i->id();
            text += " deffered commit";
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
            std::string text = "pool: transaction_id=";
            text += i->id();
            text += " add to commit list";
            return text;
        });
#endif
        ++i;
    }

    // формируем список транзакций для отправки коммитов
    rc.splice(rc.begin(), transaction_store_, b, i);

    // и возвращаем его
    return rc;
}


void pool::release(connection_id_type connection_id)
{
    lock l(mutex_);

#ifndef NDEBUG
        capst_journal.cout([&]{
            std::string text = "pool: store existing connection, ready: ";
            text += std::to_string(ready_.size());
            text += " active: ";
            text += std::to_string(active_.size());
            return text;
        });
#endif

    // перемещаем соединенеи в список готовых к работе
    ready_.splice(ready_.begin(), active_, connection_id);
    auto& conn = ready_.front();
    conn.set(ready_.begin());
}

} // namespace capst
