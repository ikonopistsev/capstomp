cmake_minimum_required(VERSION 3.10)

project(capstomp VERSION 2.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(stomptalk)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} stomptalk/include)

option(CAPSTOMP_STATIC_LIBEVENT "add static libevent" OFF)
if (CAPSTOMP_STATIC_LIBEVENT)
    set(EVENT__LIBRARY_TYPE "STATIC")
    add_subdirectory(libevent)
    include_directories(libevent/include ${CMAKE_CURRENT_BINARY_DIR}/libevent/include)
endif()

add_subdirectory(stompconn)

option(CAPSTOMP_HAVE_MY_BOOL "mysql has my_bool type" OFF)
if (CAPSTOMP_HAVE_MY_BOOL)
  add_definitions(-DHAVE_TYPE_MY_BOOL)
endif()

set(CAPSTOMP_POOL_SOCKETS "256" CACHE STRING "count of cached soktest in pool")
add_definitions("-DCAPSTOMP_POOL_SOCKETS=${CAPSTOMP_POOL_SOCKETS}")

set(CAPSTOMP_MAX_POOL_SOCKETS "4096" CACHE STRING "max sockets in pool")
add_definitions("-DCAPSTOMP_MAX_POOL_SOCKETS=${CAPSTOMP_MAX_POOL_SOCKETS}")

set(CAPSTOMP_MAX_POOL_COUNT "256" CACHE STRING "max of sockets pools")
add_definitions("-DCAPSTOMP_MAX_POOL_COUNT=${CAPSTOMP_MAX_POOL_COUNT}")

option(CAPSTOMP_TRACE_LOG "addition trase to syslog" OFF)
if (CAPSTOMP_TRACE_LOG)
    add_definitions(-DCAPSTOMP_TRACE_LOG)
endif()

add_definitions("-DCAPSTOMP_PLUGIN_VERSION=${PROJECT_VERSION}")
add_definitions("-DCAPSTOMP_SOCKET_SLOTS=${CAPSTOMP_SOCKET_SLOTS}")
add_definitions("-DHAVE_DLOPEN")

set(sources
    src/udf.cpp
    src/pool.cpp
    src/store.cpp
    src/journal.cpp
    src/connection.cpp
)

include_directories("/usr/include/mysql")

add_library(capstomp SHARED ${sources})

option(CAPSTOMP_STATIC_STDCPP "static linking of libstdc++" OFF)

if (CAPSTOMP_STATIC_STDCPP)
    target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a event stomptalk stompconn)
else()
    target_link_libraries(capstomp PRIVATE event stomptalk stompconn)
endif()

