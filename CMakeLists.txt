cmake_minimum_required(VERSION 3.7)

project(capstomp VERSION 2.2.3 LANGUAGES CXX)

set(PROJECT_VERSION_TWEAK 2)

macro(use_cxx17)
  if (CMAKE_VERSION VERSION_LESS "3.8.2")
    message(STATUS "capstomp enable c++17")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  else ()
    set (CMAKE_CXX_STANDARD 17)
    set (CMAKE_CXX_STANDARD_REQUIRED ON)
  endif ()
endmacro(use_cxx17)

use_cxx17()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

string(CONCAT CAPSTOMP_CXX_NAME "${CMAKE_CXX_COMPILER_ID}" "-" "${CMAKE_CXX_COMPILER_VERSION}")
add_definitions(-DCAPSTOMP_CXX_NAME=${CAPSTOMP_CXX_NAME})

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    option(CAPSTOMP_CLANG_LIBCXX "use clang libcxx" OFF)
    if (CAPSTOMP_CLANG_LIBCXX)
	add_compile_options("-stdlib=libc++")
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

# count of persistent tcp stomp connections per table
# usualy it one per table (if triggers only)
set(CAPSTOMP_POOL_SOCKETS "8" CACHE STRING "count of cached soktest in pool")
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

# cpack

set(CPACK_PACKAGE_VENDOR "ikonopistsev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "mysql udf plugin for sending stomp message to message broker")
set(CPACK_PACKAGE_CONTACT "admin@dev4.fun")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_STRIP_FILES TRUE)

if(EXISTS "/etc/os-release")
    # read arch
    EXECUTE_PROCESS( COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE )

    # find os release name and version
    file(STRINGS /etc/os-release OS_RELEASE_ID REGEX ^ID= )
    string(REPLACE "ID=" "" OS_RELEASE_ID ${OS_RELEASE_ID})
    string(REPLACE  "\"" "" OS_RELEASE_ID ${OS_RELEASE_ID}) #"

    file(STRINGS /etc/os-release OS_RELEASE_VERSION_ID REGEX ^VERSION_ID=)
    if (OS_RELEASE_VERSION_ID)
        string(REPLACE "VERSION_ID=" "" OS_RELEASE_VERSION_ID ${OS_RELEASE_VERSION_ID})
        string(REPLACE  "\"" "" OS_RELEASE_VERSION_ID ${OS_RELEASE_VERSION_ID}) #"
        string(CONCAT CPACK_SYSTEM_NAME ${OS_RELEASE_ID} ${OS_RELEASE_VERSION_ID} "-" ${ARCHITECTURE})
    endif()
    string(CONCAT CPACK_SYSTEM_NAME ${OS_RELEASE_ID} ${OS_RELEASE_VERSION_ID} "-" ${ARCHITECTURE})
endif()

#set(CPACK_GENERATOR "DEB")

if (CPACK_GENERATOR STREQUAL "DEB")    
    install(TARGETS capstomp LIBRARY DESTINATION lib/mysql/plugin)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ikonopistsev")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION main)
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_VERSION_TWEAK})
    set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}")
    set(CPACK_DEB_PACKAGE_COMPONENT ON)
endif()

if (CPACK_GENERATOR STREQUAL "RPM")
    install(TARGETS capstomp LIBRARY DESTINATION lib64/mysql/plugin)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST /usr /usr/lib64 /usr/lib64/mysql /usr/lib64/mysql/plugin)
    set(CPACK_RPM_PACKAGE_RELEASE ${PROJECT_VERSION_TWEAK})
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/ikonopistsev/capstomp")
endif()

include(CPack)

