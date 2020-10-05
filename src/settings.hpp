#pragma once

#include "uri.hpp"

namespace capst {

class settings
{
protected:
    // wait rabbitmq receipts
    bool receipt_{ false };

    // add timestamps to headers
    bool connection_timestamp_{ false };

    // use transaction
    bool transaction_{ true };
    bool persistent_{ false };
    std::size_t delivery_mode_{ 0u };

    std::string pool_{};

    void parse(std::string_view query);

public:
    settings() = default;

    static settings create(const uri& u);

    bool receipt() const noexcept;

    bool transaction() const noexcept;

    const std::string& pool() const noexcept
    {
        return pool_;
    }
};

class connection_settings
    : public settings
{
public:
    connection_settings() = default;

    connection_settings(const settings& other);

    bool timestamp() const noexcept;

    bool persistent() const noexcept;

    std::size_t delivery_mode() const noexcept;
};

} // namespace capst
