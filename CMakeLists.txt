cmake_minimum_required(VERSION 3.2)
project(ormpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(ENABLE_MYSQL ON)
SET(ENABLE_PG OFF)
SET(ENABLE_SQLITE3 OFF)
if (ENABLE_MYSQL)
include(FindMySQL.cmake)
add_definitions(-DORMPP_ENABLE_MYSQL)
set(SOURCE_FILES main.cpp dbng.hpp unit_test.hpp pg_types.h
        type_mapping.hpp utility.hpp entity.hpp mysql.hpp
        connection_pool.hpp ormpp_cfg.hpp)
endif()
if (ENABLE_SQLITE3)
add_definitions(-DORMPP_ENABLE_SQLITE3)
set(SOURCE_FILES main.cpp dbng.hpp unit_test.hpp pg_types.h
        type_mapping.hpp utility.hpp entity.hpp  sqlite.hpp connection_pool.hpp ormpp_cfg.hpp)
endif()
if (ENABLE_PG)
add_definitions(-DORMPP_ENABLE_PG)
set(SOURCE_FILES main.cpp dbng.hpp unit_test.hpp pg_types.h
        type_mapping.hpp utility.hpp entity.hpp  postgresql.hpp connection_pool.hpp ormpp_cfg.hpp)
endif()

INCLUDE_DIRECTORIES(
                    ${CMAKE_SOURCE_DIR}
                    ${MYSQL_INCLUDE_DIR}
                    ${CMAKE_SOURCE_DIR}/iguana
                   )

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
if (ENABLE_MYSQL)
target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
if (MSVC)
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MD")
endif()
endif()
if (ENABLE_SQLITE3)
target_link_libraries(${PROJECT_NAME} sqlite3)
endif()
if (ENABLE_PG)
target_link_libraries(${PROJECT_NAME} pq)
endif()
