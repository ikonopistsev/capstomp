cmake_minimum_required(VERSION 3.19)

project(capstomp VERSION 2.5.3 LANGUAGES CXX)

set(PROJECT_VERSION_RELEASE 1)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

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

add_subdirectory(btdef)
add_subdirectory(btpro)
add_subdirectory(stomptalk)

option(CAPSTOMP_STATIC_LIBEVENT "static libevent" OFF)
if (CAPSTOMP_STATIC_LIBEVENT)
	set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "Defines if libevent should build without the benchmark executables" FORCE)
	set(EVENT__DISABLE_DEBUG_MODE ON CACHE BOOL "Define if libevent should build without support for a debug mode" FORCE)
	set(EVENT__DISABLE_GCC_WARNINGS ON CACHE BOOL "Disable verbose warnings with GCC" FORCE)
	set(EVENT__DISABLE_OPENSSL ON CACHE BOOL "Define if libevent should build without support for OpenSSL encryption" FORCE)
	set(EVENT__DISABLE_MM_REPLACEMENT ON CACHE BOOL "Define if libevent should not allow replacing the mm functions" FORCE)
	set(EVENT__DISABLE_REGRESS ON CACHE BOOL "Disable the regress tests" FORCE)
	set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "Disable sample files" FORCE)
	set(EVENT__DISABLE_TESTS ON CACHE BOOL "If tests should be compiled or not" FORCE)
	set(EVENT__LIBRARY_TYPE "STATIC" CACHE STRING "Set library type to SHARED/STATIC/BOTH (default SHARED for MSVC, otherwise BOTH)"  FORCE)
    add_subdirectory(libevent)
endif()

option(CAPSTOMP_FIND_LIBEVENT "find libevent package" OFF)
if (CAPSTOMP_FIND_LIBEVENT)
    find_package(Libevent CONFIG REQUIRED)
    include_directories(${LIBEVENT_INCLUDE_DIRS})
endif()

# it for lib stompconn
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# after libevent
add_subdirectory(stompconn)

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
    if (CAPSTOMP_STATIC_LIBEVENT)
        target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a stomptalk stompconn event btpro btdef)
    else()
        target_link_libraries(capstomp PRIVATE -static-libgcc -static-libstdc++ -l:libstdc++.a stomptalk stompconn ${LIBEVENT_LIBRARIES} btpro btdef)
    endif()
else()
    if (CAPSTOMP_STATIC_LIBEVENT)
        target_link_libraries(capstomp PRIVATE event_core_static stomptalk stompconn btpro btdef)
    else()
        target_link_libraries(capstomp PRIVATE event_core stomptalk stompconn btpro btdef)
    endif()
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
    set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_VERSION_RELEASE})
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE ${CAPSTOMP_HOME_PAGE})
    set (CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/dpkg/postinst;${CMAKE_CURRENT_SOURCE_DIR}/dpkg/prerm")
endif()

if (CPACK_GENERATOR STREQUAL "RPM")
    set(CAPSTOMP_LIBRARY_DESTINATION "/usr/lib64/capstomp")
    install(TARGETS capstomp LIBRARY DESTINATION ${CAPSTOMP_LIBRARY_DESTINATION})
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST /usr /usr/lib64)

    set(CPACK_RPM_PACKAGE_RELEASE ${PROJECT_VERSION_RELEASE})
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
