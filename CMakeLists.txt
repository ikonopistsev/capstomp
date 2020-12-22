cmake_minimum_required(VERSION 3.7)

project(capstomp VERSION 2.3.4 LANGUAGES CXX)

set(PROJECT_VERSION_TWEAK 1)

macro(use_cxx17)
  if (CMAKE_VERSION VERSION_LESS "3.8.2")
    message(STATUS "capstomp enable C++17")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  else ()
    set (CMAKE_CXX_STANDARD 17)
    set (CMAKE_CXX_STANDARD_REQUIRED ON)
  endif ()
endmacro(use_cxx17)

# enable C++17 for cmake 3.7
use_cxx17()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CAPSTOMP_STATIC_LIBEVENT "static libevent" OFF)

# unsing clang libc++ if needed
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    option(CAPSTOMP_CLANG_LIBCXX "use clang libcxx for capstomp" OFF)
    if (CAPSTOMP_CLANG_LIBCXX)
        add_compile_options("-stdlib=libc++")
    endif()
else()
    option(CAPSTOMP_STATIC_STDCPP "static linking of libstdc++" OFF)
endif()

# compiller name for library initilize message
string(CONCAT CAPSTOMP_CXX_NAME "${CMAKE_CXX_COMPILER_ID}" "-" "${CMAKE_CXX_COMPILER_VERSION}")
add_definitions(-DCAPSTOMP_CXX_NAME=${CAPSTOMP_CXX_NAME})

option(CAPSTOMP_HAVE_MY_BOOL "mysql has my_bool type" OFF)
if (CAPSTOMP_HAVE_MY_BOOL)
  add_definitions(-DHAVE_TYPE_MY_BOOL)
endif()

option(CAPSTOMP_STATE_DEBUG "state of connection" OFF)
if (CAPSTOMP_STATE_DEBUG)
  add_definitions(-DCAPSTOMP_STATE_DEBUG)
endif()

# count of persistent tcp stomp connections per table
# usualy it one per table (if triggers only)
set(CAPSTOMP_POOL_SOCKETS "16" CACHE STRING "count of cached soktest in pool")
add_definitions("-DCAPSTOMP_POOL_SOCKETS=${CAPSTOMP_POOL_SOCKETS}")

# number of maximum connections per table (parallel runnig sql querys)
set(CAPSTOMP_MAX_POOL_SOCKETS "250" CACHE STRING "max sockets in pool")
add_definitions("-DCAPSTOMP_MAX_POOL_SOCKETS=${CAPSTOMP_MAX_POOL_SOCKETS}")

# one pull used per table (maximum tables)
set(CAPSTOMP_MAX_POOL_COUNT "250" CACHE STRING "max of sockets pools")
add_definitions("-DCAPSTOMP_MAX_POOL_COUNT=${CAPSTOMP_MAX_POOL_COUNT}")

option(CAPSTOMP_TRACE_LOG "addition trase to syslog" OFF)
if (CAPSTOMP_TRACE_LOG)
    add_definitions(-DCAPSTOMP_TRACE_LOG)
endif()

add_definitions("-DCAPSTOMP_PLUGIN_VERSION=${PROJECT_VERSION}")

add_subdirectory(stomptalk)

# it for lib stompconn
include_directories(${CMAKE_CURRENT_SOURCE_DIR} stomptalk/include)

add_subdirectory(stompconn)

if (CAPSTOMP_STATIC_LIBEVENT)
    set(EVENT__LIBRARY_TYPE "STATIC")
    add_subdirectory(libevent)
    include_directories(libevent/include ${CMAKE_CURRENT_BINARY_DIR}/libevent/include)
endif()

set(sources
    src/udf.cpp
    src/conf.cpp
    src/pool.cpp
    src/store.cpp
    src/journal.cpp
    src/connection.cpp
    src/settings.cpp
)

# include mysql headers
set(MySQL_INCLUDE_DIRS "/usr/include/mysql")
if (NOT EXISTS ${MySQL_INCLUDE_DIRS})
    find_package(PkgConfig)
    pkg_check_modules(MySQL REQUIRED mysqlclient>=5.7)
endif()

include_directories(${MySQL_INCLUDE_DIRS})

add_library(capstomp SHARED ${sources})

if (CAPSTOMP_STATIC_STDCPP)
    target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a event stomptalk stompconn)
else()
    target_link_libraries(capstomp PRIVATE event stomptalk stompconn)
endif()

# cpack only for x86_64
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")

set(CPACK_PACKAGE_VENDOR "ikonopistsev")
set(CAPSTOMP_HOME_PAGE "https://github.com/ikonopistsev/capstomp")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "mysql udf plugin for sending stomp message to message broker")
set(CPACK_PACKAGE_CONTACT "admin@dev4.fun")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_STRIP_FILES TRUE)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if (EXISTS "/etc/os-release")
    # find os release name and version
    file(STRINGS /etc/os-release OS_RELEASE_ID REGEX ^ID= )
    if (OS_RELEASE_ID)
        string(REPLACE "ID=" "" OS_RELEASE_ID ${OS_RELEASE_ID})
        string(REPLACE  "\"" "" OS_RELEASE_ID ${OS_RELEASE_ID}) #"

        file(STRINGS /etc/os-release OS_RELEASE_VERSION_ID REGEX ^VERSION_ID=)
        if (OS_RELEASE_VERSION_ID)
            string(REPLACE "VERSION_ID=" "" OS_RELEASE_VERSION_ID ${OS_RELEASE_VERSION_ID})
            string(REPLACE  "\"" "" OS_RELEASE_VERSION_ID ${OS_RELEASE_VERSION_ID}) #"
            string(CONCAT CPACK_SYSTEM_NAME ${OS_RELEASE_ID} ${OS_RELEASE_VERSION_ID})
        else()
            string(CONCAT CPACK_SYSTEM_NAME ${OS_RELEASE_ID})
        endif()

        if (CMAKE_SYSTEM_PROCESSOR)
            string(CONCAT CPACK_SYSTEM_NAME ${CPACK_SYSTEM_NAME} "-" ${CMAKE_SYSTEM_PROCESSOR})
        endif()
    endif()
endif()

if (CPACK_GENERATOR STREQUAL "DEB")
    set(CAPSTOMP_LIBRARY_DESTINATION "/usr/lib/capstomp")
    install(TARGETS capstomp LIBRARY DESTINATION ${CAPSTOMP_LIBRARY_DESTINATION})

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ikonopistsev")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION main)
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_VERSION_TWEAK})
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE ${CAPSTOMP_HOME_PAGE})
    set (CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/dpkg/postinst;${CMAKE_CURRENT_SOURCE_DIR}/dpkg/prerm")
endif()

if (CPACK_GENERATOR STREQUAL "RPM")
    set(CAPSTOMP_LIBRARY_DESTINATION "/usr/lib64/capstomp")
    install(TARGETS capstomp LIBRARY DESTINATION ${CAPSTOMP_LIBRARY_DESTINATION})
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST /usr /usr/lib64)

    set(CPACK_RPM_PACKAGE_RELEASE ${PROJECT_VERSION_TWEAK})
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
    set(CPACK_RPM_PACKAGE_URL ${CAPSTOMP_HOME_PAGE})
    SET(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/postinst")
    SET(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/postrm")
endif()

if (CPACK_GENERATOR STREQUAL "TGZ")
    install(TARGETS capstomp LIBRARY DESTINATION ".")
endif()

include(CPack)

endif()
