#include "journal.hpp"

#include <syslog.h>
#include <cassert>

using namespace capst;

journal::journal() noexcept
    : mask_(LOG_UPTO(LOG_NOTICE))
{
    openlog("capstomp", LOG_ODELAY, LOG_USER);
    // нет смысла делать setlogmask
    // тк проверку уровня лога делаем мы сами
}

journal::~journal() noexcept
{
    closelog();
}

void journal::output(int level, const char *str) const noexcept
{
    assert(str);
    // %s из-за ворнинга
    syslog(level, "%s", str);
}

int journal::error_level() const noexcept
{
    return LOG_ERR;
}

int journal::notice_level() const noexcept
{
    return LOG_NOTICE;
}

bool journal::level_allow(int level) const noexcept
{
    return (mask_ & LOG_MASK(level)) != 0;
}
