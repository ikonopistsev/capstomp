#pragma once

#include <string>
#include <algorithm>
#include <string_view>

namespace cs {

class url
{
    std::string_view protocol_{};
    std::string_view user_{};
    std::string_view passcode_{};
    std::string_view host_{};
    std::string_view port_{};
    std::string_view vhost_{};
    static constexpr auto npos = std::string_view::npos;
    static constexpr std::string_view stomp = "stomp";

    void split_auth(std::string_view auth) noexcept
    {
        auto i = auth.find(':');
        if (i == npos)
        {
            user_ = auth;
        }
        else
        {
            user_ = auth.substr(0, i);
            passcode_ = auth.substr(i + 1);
        }
    }

    void split_addr(std::string_view addr) noexcept
    {
        auto i = addr.find(':');
        if (i == npos)
        {
            host_ = addr;
        }
        else
        {
            host_ = addr.substr(0, i);
            port_ = addr.substr(i + 1);
        }
    }

    void split_host(std::string_view host) noexcept
    {
        auto i = host.find('@');
        if (i == npos)
        {
            split_addr(host);
        }
        else
        {
            split_auth(host.substr(0, i));
            split_addr(host.substr(i + 1));
        }
    }

    void parse(std::string_view data) noexcept
    {
        constexpr std::string_view prot_end = "://";

        auto f = data.find(prot_end);

        if (f == npos)
            return;

        protocol_ = data.substr(0, f);

        f += prot_end.size();
        if (f != npos)
        {
            auto v = data.find('/', f);
            if (v != npos)
            {
                vhost_ = data.substr(v + 1);
                v -= f;
            }

            split_host(data.substr(f, v));
        }
    }

public:

    url() = default;

    url(std::string_view data) noexcept
    {
        parse(data);
    }

    std::string_view protocol() const noexcept
    {
        return protocol_;
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
        return host_;
    }

    std::string_view port() const noexcept
    {
        if (port_.empty())
        {
            if (*this)
            {
                constexpr std::string_view port("61613");
                return port;
            }
        }

        return port_;
    }

    std::string addr() const noexcept
    {
        std::string rc;
        auto h = host();
        auto p = port();
        if (!(h.empty() || p.empty()))
        {
            rc += h;
            rc += ':';
            rc += p;
        }
        return rc;
    }

    std::string_view vhost() const noexcept
    {
        return vhost_;
    }

    operator bool() const noexcept
    {
        return protocol_ == stomp;
    }
};

} // namespace capstomp
