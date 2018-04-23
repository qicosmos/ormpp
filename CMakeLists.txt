cmake_minimum_required(VERSION 3.2)
project(ormpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++17")

set(SOURCE_FILES main.cpp dbng.hpp unit_test.hpp pg_types.h
        type_mapping.hpp utility.hpp entity.hpp mysql.hpp
        sqlite.hpp postgresql.hpp connection_pool.hpp ormpp_cfg.hpp)

INCLUDE_DIRECTORIES(
                    ${CMAKE_SOURCE_DIR}/iguana
                   )

add_executable(ormpp ${SOURCE_FILES})
target_link_libraries(ormpp mysqlclient sqlite3 pq)