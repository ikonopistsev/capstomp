cmake_minimum_required(VERSION 3.10)

project(capstomp VERSION 2.1.5 LANGUAGES CXX)
set(PROJECT_REVISION 4)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    option(CAPSTOMP_CLANG_LIBCXX "use clang libcxx" OFF)
    if (CAPSTOMP_CLANG_LIBCXX)
    add_compile_options("-nostdinc++")
    include_directories("/usr/lib/llvm-11/include/c++/v1" "/usr/include/c++/v1")
    endif()
endif()

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

set(CAPSTOMP_POOL_SOCKETS "8" CACHE STRING "count of cached soktest in pool")
add_definitions("-DCAPSTOMP_POOL_SOCKETS=${CAPSTOMP_POOL_SOCKETS}")

set(CAPSTOMP_MAX_POOL_SOCKETS "128" CACHE STRING "max sockets in pool")
add_definitions("-DCAPSTOMP_MAX_POOL_SOCKETS=${CAPSTOMP_MAX_POOL_SOCKETS}")

set(CAPSTOMP_MAX_POOL_COUNT "256" CACHE STRING "max of sockets pools")
add_definitions("-DCAPSTOMP_MAX_POOL_COUNT=${CAPSTOMP_MAX_POOL_COUNT}")

option(CAPSTOMP_TRACE_LOG "addition trase to syslog" OFF)
if (CAPSTOMP_TRACE_LOG)
    add_definitions(-DCAPSTOMP_TRACE_LOG)
endif()

add_definitions("-DCAPSTOMP_PLUGIN_VERSION=${PROJECT_VERSION}")
add_definitions("-DHAVE_DLOPEN")

set(sources
    src/udf.cpp
    src/conf.cpp
    src/pool.cpp
    src/store.cpp
    src/journal.cpp
    src/connection.cpp
    src/settings.cpp
)

include_directories("/usr/include/mysql")

add_library(capstomp SHARED ${sources})

option(CAPSTOMP_STATIC_STDCPP "static linking of libstdc++" OFF)
if (CAPSTOMP_STATIC_STDCPP)
    target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a event stomptalk stompconn)
else()
    target_link_libraries(capstomp PRIVATE event stomptalk stompconn)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (CAPSTOMP_CLANG_LIBCXX)
        target_link_options(capstomp PRIVATE "-stdlib=libc++")
    endif()
endif()


# cpack
install(TARGETS capstomp LIBRARY DESTINATION lib/mysql/plugin)

set(CAPSTOMP_CPACK_TARGET "deb10" CACHE STRING "deb10 sufix")

set(CPACK_PACKAGE_VENDOR "ikonopistsev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "mysql udf plugin for sending stomp message to message broker")
set(CPACK_PACKAGE_CONTACT "admin@dev4.fun")
set(CPACK_STRIP_FILES TRUE)

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ikonopistsev")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION main)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_REVISION})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

IF(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    FIND_PROGRAM(DPKG_CMD dpkg)
    IF(NOT DPKG_CMD)
        MESSAGE(STATUS "Can not find dpkg in your path, default to i386.")
        SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE i386)
    ENDIF(NOT DPKG_CMD)
    EXECUTE_PROCESS(COMMAND "${DPKG_CMD}" --print-architecture
        OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDIF(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)

set(CPACK_PACKAGE_FILE_NAME ${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}+${CAPSTOMP_CPACK_TARGET}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE})

include(CPack)



