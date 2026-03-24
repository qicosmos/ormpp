cmake_minimum_required(VERSION 3.15)
project(ormpp)

if(ENABLE_SQLITE3_CODEC)
    include(cmake/openssl.cmake)
endif()
include(cmake/build.cmake)
include(cmake/develop.cmake)
include(cmake/platform.cmake)
include(cmake/dependency.cmake)

# Each database backend can be enabled independently.
# Multiple backends can coexist in the same binary.
if (ENABLE_PG)
    include(cmake/pgsql.cmake)
    include_directories(${PGSQL_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_PG)
endif()

if (ENABLE_MYSQL)
    include(cmake/mysql.cmake)
    include_directories(${MYSQL_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_MYSQL)
endif()

if (ENABLE_MARIADB)
    include(cmake/mariadb.cmake)
    include_directories(${MARIADB_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_MYSQL)
endif()

if (ENABLE_SQLITE3)
    add_definitions(-DORMPP_ENABLE_SQLITE3)
endif()

include_directories(ormpp)
include_directories(./)

if (BUILD_UNIT_TESTS)
    add_subdirectory(${ormpp_SOURCE_DIR}/tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(${ormpp_SOURCE_DIR}/example)
endif ()
