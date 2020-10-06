#pragma once

#include "uri.hpp"

namespace capst {

class settings
{
protected:
    // wait rabbitmq receipts
    bool receipt_{ false };
    // add timestamps to headers
    bool timestamp_{ false };

    bool persistent_{ false };
    // use transaction
    bool transaction_{ false };

    void parse(std::string_view query);

public:
    settings() = default;

    static settings create(const uri& u);

    bool receipt() const noexcept;

    bool timestamp() const noexcept;

    bool persistent() const noexcept;

    bool transaction() const noexcept;

};

} // namespace capst
