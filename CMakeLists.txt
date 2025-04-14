cmake_minimum_required(VERSION 3.15)
project(ormpp)

set(ENABLE_SQLITE3 ON)
set(SQLITE3_CODEC ON)
set(BUILD_UNIT_TESTS ON)
add_definitions(
    -DORMPP_ENABLE_SQLITE3 
    -DSQLITE_HAS_CODEC
)

if(SQLITE3_CODEC)
include(cmake/openssl.cmake)
endif()
include(cmake/build.cmake)
include(cmake/develop.cmake)
include(cmake/platform.cmake)
include(cmake/dependency.cmake)

if (ENABLE_PG)
    include_directories(${PGSQL_INCLUDE_DIR} ormpp)
elseif(ENABLE_MYSQL)
    include_directories(${MYSQL_INCLUDE_DIR} ormpp)
elseif(ENABLE_MARIADB)
    include_directories(${MARIADB_INCLUDE_DIR} ormpp)
else()
    include_directories(ormpp)
endif()

include_directories(./)

if (BUILD_UNIT_TESTS)
    add_subdirectory(${ormpp_SOURCE_DIR}/tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(${ormpp_SOURCE_DIR}/example)
endif ()