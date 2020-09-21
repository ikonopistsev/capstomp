#pragma once

#include "uri.hpp"

#include <tuple>
#include <string>

namespace capst {

template<class T>
class endpoint
{
    using value_type = T;
    using self_type = std::tuple<value_type,
        value_type, value_type, value_type>;

    self_type self_{};

public:
    endpoint() = default;

    explicit endpoint(const uri& uri)
        : self_(uri.user(), uri.addr(), uri.path(), uri.fragment())
    {   }

    bool operator==(const endpoint& other) const noexcept
    {
        return self_ == other.self_;
    }

    auto user() const noexcept
    {
        return std::get<0>(self_);
    }

    auto addr() const noexcept
    {
        return std::get<1>(self_);
    }

    auto path() const noexcept
    {
        return std::get<2>(self_);
    }

    auto fragment() const noexcept
    {
        return std::get<3>(self_);
    }

    void clear() noexcept
    {
        self_ = self_type();
    }

    auto str() const
    {
        auto u = user();
        auto a = addr();
        auto p = path();
        auto f = fragment();

        std::string rc;
        rc.reserve(u.size() + a.size() + p.size() + f.size() + 32);

        if (!u.empty())
            rc += "user:" + u;

        if (!a.empty())
            rc += " addr:" + a;

        if (!p.empty())
            rc += " path:" + p;

        if (!f.empty())
            rc += " fragment:" + f;

        return rc;
    }

    struct hf
    {
        auto operator()(const endpoint& e) const noexcept
        {
            auto u = e.user();
            auto a = e.addr();
            auto p = e.path();
            auto f = e.fragment();

            value_type t;
            t.reserve(u.size() + a.size() + p.size() + f.size() + 2);

            t += u;
            t += '@';
            t += a;
            t += p;
            t += '#';
            t += f;

            std::hash<value_type> h;
            return h(t);
        }
    };
};

} // namespace capst
