#pragma once

#include "btdef/date.hpp"
#include <string_view>

namespace capst {

template<class T, class C>
class transaction
{
    using string_type = T;
    using connection_type = C;
    using ready_type = std::size_t;
    using date_type = btdef::date;

    using data_type =
        std::tuple<string_type, date_type, ready_type, connection_type>;

    data_type that_{ string_type(),
        date_type::now(), ready_type(), connection_type()};


public:
    transaction() = default;

    transaction(std::string_view id, connection_type connection) noexcept
        : that_(id, date_type::now(), ready_type(), connection)
    {   }

    auto id() const noexcept
    {
        auto& i = std::get<string_type>(that_);
        return std::string_view(i.data(), i.size());
    }

    auto time() const noexcept
    {
        return std::get<date_type>(that_);
    }

    auto ready() const noexcept
    {
        return std::get<ready_type>(that_) > ready_type();
    }

    auto connection() const noexcept
    {
        return std::get<connection_type>(that_);
    }

    template<class V>
    void set(V value)
    {
        std::get<V>(that_) = value;
    }

    void set(bool ready) noexcept
    {
        set(static_cast<ready_type>(ready));
    }
};

} // namespace capst
