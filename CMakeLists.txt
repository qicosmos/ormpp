cmake_minimum_required(VERSION 3.15)
project(ormpp)

if(ENABLE_SQLITE3_CODEC)
    include(cmake/openssl.cmake)
endif()
include(cmake/build.cmake)
include(cmake/develop.cmake)
include(cmake/platform.cmake)
include(cmake/dependency.cmake)

# Auto-detect available database backends
include(cmake/pgsql.cmake)
if (PGSQL_FOUND)
    include_directories(${PGSQL_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_PG)
endif()

include(cmake/mysql.cmake)
if (MYSQL_FOUND)
    include_directories(${MYSQL_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_MYSQL)
endif()

include(cmake/mariadb.cmake)
if (MARIADB_FOUND)
    include_directories(${MARIADB_INCLUDE_DIR})
    add_definitions(-DORMPP_ENABLE_MYSQL)
endif()

# sqlite3 is always available (bundled)
add_definitions(-DORMPP_ENABLE_SQLITE3)

include_directories(ormpp)
include_directories(./)

if (BUILD_UNIT_TESTS)
    add_subdirectory(${ormpp_SOURCE_DIR}/tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(${ormpp_SOURCE_DIR}/example)
endif ()
