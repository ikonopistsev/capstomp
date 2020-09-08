#pragma once

#include <memory>
#include <string_view>
#include <cassert>
#include "event2/http.h"

namespace capst {

class uri
{
public:
    using handle_type = evhttp_uri*;

private:
    std::unique_ptr<evhttp_uri, decltype(&evhttp_uri_free)> handle_;
    std::string_view user_{};
    std::string_view passcode_{};

    constexpr static std::string_view stomp = "stomp";
    static constexpr auto npos = std::string_view::npos;

    auto split_auth(std::string_view auth) noexcept
    {
        auto i = auth.find(':');
        if (i == npos)
            return std::make_pair(auth, std::string_view());

        return std::make_pair(auth.substr(0, i), auth.substr(i + 1));
    }

public:
    uri()
        : handle_(evhttp_uri_new(), evhttp_uri_free)
    {
        if (!handle_)
            throw std::runtime_error("evhttp_uri_new");
    }

    uri(std::string_view str)
        : handle_(nullptr, evhttp_uri_free)
    {
        auto p = evhttp_uri_parse(str.data());
        if (!p)
            throw std::runtime_error("evhttp_uri_parse");

        auto ui = evhttp_uri_get_userinfo(p);
        if (ui)
        {
            auto cr = split_auth(ui);
            user_ = std::get<0>(cr);
            passcode_ = std::get<1>(cr);
        }

        handle_.reset(p);
    }

    handle_type handle() const noexcept
    {
        auto handle = handle_.get();
        assert(handle);
        return handle;
    }

    std::string_view scheme() const noexcept
    {
        auto result = evhttp_uri_get_scheme(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    std::string_view userinfo() const noexcept
    {
        auto result = evhttp_uri_get_userinfo(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    std::string_view user() const noexcept
    {
        return user_;
    }

    std::string_view passcode() const noexcept
    {
        return passcode_;
    }

    std::string_view host() const noexcept
    {
        auto result = evhttp_uri_get_host(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    int port() const noexcept
    {
        auto rc = evhttp_uri_get_port(handle());
        if ((rc == -1) && (scheme() == stomp))
            return 61613;
        return rc;
    }

    std::string_view path() const noexcept
    {
        auto result = evhttp_uri_get_path(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    std::string_view rpath() const noexcept
    {
        auto p = path();
        return (!p.empty() && p[0] == '/') ? p.substr(1) : p;
    }

    std::string_view query() const noexcept
    {
        auto result = evhttp_uri_get_query(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    std::string_view fragment() const noexcept
    {
        auto result = evhttp_uri_get_fragment(handle());
        if (result)
            return std::string_view(result);

        return std::string_view();
    }

    std::string addr() const noexcept
    {
        std::string rc;
        auto h = host();
        auto p = port();
        if (!(h.empty() || (p <= 0)))
        {
            auto sz = h.size() + 5;
            if (sz > rc.capacity())
                rc.resize(sz);
            rc += h;
            rc += ':';
            rc += std::to_string(p);
        }
        return rc;
    }
};

} // namespace capst
