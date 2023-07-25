cmake_minimum_required(VERSION 3.15)
project(ormpp)

include(cmake/build.cmake)
include(cmake/develop.cmake)
include(cmake/platform.cmake)
include(cmake/dependency.cmake)

if (ENABLE_MYSQL)
    include_directories(${MYSQL_INCLUDE_DIR} include)
else()
    include_directories(include)
endif()

include_directories(./)

if (BUILD_UNIT_TESTS)
    add_subdirectory(${ormpp_SOURCE_DIR}/tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(${ormpp_SOURCE_DIR}/example)
endif ()