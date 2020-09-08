#pragma once

#include <string_view>
#include "btdef/text.hpp"
#include "btdef/date.hpp"

namespace capst {

template<class T>
class transaction
{
    using string_type = T;
    using data_type = std::tuple<string_type, btdef::date, std::size_t>;
    data_type that_{ std::string_view(), btdef::date::now(), 0u };

    auto& ready_val() noexcept
    {
        return std::get<2>(that_);
    }

    auto& ready_val() const noexcept
    {
        return std::get<2>(that_);
    }

public:
    transaction() = default;

    transaction(std::string_view id) noexcept
        : that_(id, btdef::date::now(), false)
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
};

} // namespace capst
