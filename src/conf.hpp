#pragma once

#include <cstddef>

namespace capst {

class conf
{
public:
    static constexpr auto timeout_min = std::size_t{300u};
    
private:
    static constexpr auto timeout_def = std::size_t{25000u};
    static constexpr auto timeout_max = std::size_t{90000u};
    volatile std::size_t timeout_ = {timeout_def};

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

    static constexpr auto request_limit_min = std::size_t{4u};
    static constexpr auto request_limit_def = std::size_t{20480u};
    // число запросов, переданных через соединение,
    // до принудительного получение квитанции от кролика
    volatile std::size_t request_limit_ = {request_limit_def};

    static constexpr auto verbose_max = std::size_t{2u};
    volatile std::size_t verbose_ = std::size_t{1u};

    conf() = default;

    static conf& inst() noexcept;

public:

    static inline auto timeout() noexcept
    {
        return inst().timeout_;
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

    static inline std::size_t verbose() noexcept
    {
        return inst().verbose_;
    }

    static void set_timeout(std::size_t value) noexcept;

    static void set_max_pool_count(std::size_t value) noexcept;

    static void set_max_pool_sockets(std::size_t value) noexcept;

    static void set_pool_sockets(std::size_t value) noexcept;

    static void set_request_limit(std::size_t value) noexcept;

    static void set_verbose(std::size_t value) noexcept;
};

} // namespace capst
