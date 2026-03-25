cmake_minimum_required(VERSION 3.15)
project(ormpp)

if(ENABLE_SQLITE3_CODEC)
    include(cmake/openssl.cmake)
endif()
include(cmake/build.cmake)
include(cmake/develop.cmake)
include(cmake/platform.cmake)
include(cmake/dependency.cmake)


include_directories(ormpp)
include_directories(./)

if (BUILD_UNIT_TESTS)
    add_subdirectory(${ormpp_SOURCE_DIR}/tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(${ormpp_SOURCE_DIR}/example)
endif ()
