cmake_minimum_required(VERSION 3.10.1)

file(READ "${RELATIVE_SOURCE_DIR}version.txt" FILE_VER)
file(READ "${RELATIVE_SOURCE_DIR}revision.txt" FILE_REV)

# менять верисю пакета тут
project(capstomp VERSION ${FILE_VER} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#set(EVENT__DISABLE_MBEDTLS ON)
#set(EVENT__DISABLE_OPENSSL ON)
#set(EVENT__LIBRARY_TYPE STATIC)

#set(EVENT__DISABLE_BENCHMARK ON)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__DISABLE_REGRESS ON)
#set(EVENT__DISABLE_SAMPLES ON)
#set(EVENT__DISABLE_TESTS ON)


add_subdirectory(stomptalk)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} stomptalk/include)

option(CAPSTOMP_STATIC_LIBEVENT "add static libevent" ON)
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

set(CAPSTOMP_SOCKET_SLOTS "10" CACHE STRING "socket slots" FORCE)

add_definitions(-DCAPSTOMP_REVISION=${FILE_VER})
add_definitions(-DCAPSTOMP_SOCKET_SLOTS=${CAPSTOMP_SOCKET_SLOTS})

set(sources
    src/capstomp.cpp
    src/journal.cpp
    src/udf.cpp
)

include_directories("/usr/include/mysql")
add_definitions("-DHAVE_DLOPEN")
add_library(capstomp SHARED ${sources})

option(CAPSTOMP_STATIC_STDCPP "static linking of libstdc++" OFF)

if (CAPSTOMP_STATIC_STDCPP)
    target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a event stomptalk stompconn)
else()
    target_link_libraries(capstomp PRIVATE event stomptalk stompconn)
endif()

