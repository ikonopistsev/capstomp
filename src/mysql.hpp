#pragma once

#include <mysql.h>

// FIX my_bool was removed in 8.0.1
#ifndef HAVE_TYPE_MY_BOOL
#include <stdbool.h>
typedef bool my_bool;
#endif
