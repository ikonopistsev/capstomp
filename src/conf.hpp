#pragma once

#include <cstddef>

namespace capst {

class conf
{
    static constexpr auto read_timeout_min = std::size_t{300u};
    static constexpr auto read_timeout_def = std::size_t{20000u};
    volatile std::size_t read_timeout_ = {read_timeout_def};

    static constexpr auto max_pool_count_min = std::size_t{16u};
    static constexpr auto max_pool_count_def
        = std::size_t{CAPSTOMP_MAX_POOL_COUNT};
    volatile std::size_t max_pool_count_ = {max_pool_count_def};

    static constexpr auto max_pool_sockets_min = std::size_t{8u};
    static constexpr auto max_pool_sockets_def
        = std::size_t{CAPSTOMP_MAX_POOL_SOCKETS};
    volatile std::size_t max_pool_sockets_ = {max_pool_sockets_def};

    static constexpr auto pool_sockets_min = std::size_t{4u};
    static constexpr auto pool_sockets_def = std::size_t{CAPSTOMP_POOL_SOCKETS};
    // число одновременно подключенных сокетов от udf к брокеру
    volatile std::size_t pool_sockets_ = {pool_sockets_def};

    static constexpr auto request_limit_min = std::size_t{64u};
    static constexpr auto request_limit_def = std::size_t{4096u};
    // число запросов, переданных через соединение,
    // до его принудительного закрытия
    volatile std::size_t request_limit_ = {request_limit_def};

    volatile std::size_t enable_ = std::size_t{1u};

    conf() = default;

    static conf& inst() noexcept;

public:

    static inline auto read_timeout() noexcept
    {
        return inst().read_timeout_;
    }

    static inline auto max_pool_count() noexcept
    {
        return inst().max_pool_count_;
    }

    static inline auto max_pool_sockets() noexcept
    {
        return inst().max_pool_sockets_;
    }

    static inline auto pool_sockets() noexcept
    {
        return inst().pool_sockets_;
    }

    static inline auto request_limit() noexcept
    {
        return inst().request_limit_;
    }

    static inline bool enable() noexcept
    {
        return inst().enable_ != 0u;
    }

    static void set_read_timeout(std::size_t value) noexcept;

    static void set_max_pool_count(std::size_t value) noexcept;

    static void set_max_pool_sockets(std::size_t value) noexcept;

    static void set_pool_sockets(std::size_t value) noexcept;

    static void set_request_limit(std::size_t value) noexcept;

    static void set_enable(std::size_t value) noexcept;
};

} // namespace capst
