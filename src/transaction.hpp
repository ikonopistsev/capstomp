#pragma once

#include <string_view>
#include "btdef/text.hpp"
#include "btdef/date.hpp"

namespace capst {

template<class T, class C>
class transaction
{
    using string_type = T;
    using connection_type = C;
    using data_type = std::tuple<string_type, btdef::date, std::size_t, connection_type>;
    data_type that_{ std::string_view(), btdef::date::now(), 0u, connection_type()};

    auto& ready_val() noexcept
    {
        return std::get<2>(that_);
    }

    auto ready_val() const noexcept
    {
        return std::get<2>(that_);
    }

    auto& connection_val() noexcept
    {
        return std::get<3>(that_);
    }

    auto connection_val() const noexcept
    {
        return std::get<3>(that_);
    }

public:
    transaction() = default;

    transaction(std::string_view id, connection_type connection) noexcept
        : that_(id, btdef::date::now(), false, connection)
    {   }

    auto id() const noexcept
    {
        auto& i = std::get<0>(that_);
        return std::string_view(i.data(), i.size());
    }

    auto time() const noexcept
    {
        return std::get<1>(that_);
    }

    bool ready() const noexcept
    {
        return ready_val() > 0u;
    }

    void set(bool ready) noexcept
    {
        ready_val() = static_cast<std::size_t>(ready);
    }

    void set(connection_type connection)
    {
        connection_val() = connection;
    }

    connection_type connection() const noexcept
    {
        return connection_val();
    }
};

} // namespace capst
