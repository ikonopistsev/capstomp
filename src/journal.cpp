#include "journal.hpp"

#include <syslog.h>
#include <cassert>

using namespace capst;

journal::journal() noexcept
    : mask_(LOG_UPTO(LOG_NOTICE))
{
    openlog(nullptr, LOG_ODELAY|LOG_PID, LOG_USER);
    // нет смысла делать setlogmask
    // тк проверку уровня лога делаем мы сами
}

journal::~journal() noexcept
{
    closelog();
}

void journal::set_level(int level) noexcept
{
    if (level > 1)
        mask_ = LOG_UPTO(LOG_DEBUG);
    else
        mask_ = (level == 1) ? LOG_UPTO(LOG_NOTICE) : LOG_UPTO(LOG_ERR);
}

bool journal::allow_trace() const noexcept
{
    return level_allow(trace_level());
}

void journal::output(int level, const char *str) const noexcept
{
    assert(str);
    // %s из-за ворнинга
    syslog(level, "capstomp %s", str);
}

int journal::error_level() const noexcept
{
    return LOG_ERR;
}

int journal::notice_level() const noexcept
{
    return LOG_NOTICE;
}

int journal::trace_level() const noexcept
{
    return LOG_DEBUG;
}

bool journal::level_allow(int level) const noexcept
{
    return (mask_ & LOG_MASK(level)) != 0;
}

