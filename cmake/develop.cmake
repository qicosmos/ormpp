# extra
option(BUILD_EXAMPLES "Build examples" ON)

# bench test
option(BUILD_BENCHMARK "Build benchmark" ON)

# unit test
option(BUILD_UNIT_TESTS "Build unit tests" ON)
if(BUILD_UNIT_TESTS)
    enable_testing()
endif()

# coverage test
option(COVERAGE_TEST "Build with unit test coverage" OFF)
if(COVERAGE_TEST)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage --coverage")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
    endif()
endif()

macro(check_asan _RESULT)
    include(CheckCXXSourceRuns)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
    check_cxx_source_runs(
            [====[
int main()
{
  return 0;
}
]====]
            ${_RESULT}
    )
    unset(CMAKE_REQUIRED_FLAGS)
endmacro()

# Enable address sanitizer
option(ENABLE_SANITIZER "Enable sanitizer(Debug+Gcc/Clang/AppleClang)" ON)
if(ENABLE_SANITIZER AND NOT MSVC)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        check_asan(HAS_ASAN)
        if(HAS_ASAN)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
        else()
            message(WARNING "sanitizer is no supported with current tool-chains")
        endif()
    else()
        message(WARNING "Sanitizer supported only for debug type")
    endif()
endif()

# open log
option(ENABLE_LOG "Enable log" OFF)
if(ENABLE_LOG)
    add_definitions(-DORMPP_ENABLE_LOG)
endif()

# sqlite3 is always available (bundled)
message(STATUS "ENABLE_SQLITE3 (bundled)")
add_definitions(-DORMPP_ENABLE_SQLITE3)
if(ENABLE_SQLITE3_CODEC)
    message(STATUS "ENABLE_SQLITE3_CODEC")
    add_definitions(-DSQLITE_HAS_CODEC)
endif()

option(ENABLE_MYSQL "Enable mysql" OFF)
if (ENABLE_MYSQL)
    include(cmake/mysql.cmake)
    if (MYSQL_FOUND)
        message(STATUS "ENABLE_MYSQL")
        include_directories(${MYSQL_INCLUDE_DIR})
        add_definitions(-DORMPP_ENABLE_MYSQL)
    else()
        message(FATAL_ERROR "ENABLE_MYSQL is ON but MySQL library not found")
    endif()
endif()

option(ENABLE_MARIADB "Enable mariadb" OFF)
if (ENABLE_MARIADB)
    include(cmake/mariadb.cmake)
    if (MARIADB_FOUND)
        message(STATUS "ENABLE_MARIADB")
        include_directories(${MARIADB_INCLUDE_DIR})
        add_definitions(-DORMPP_ENABLE_MYSQL)
    else()
        message(FATAL_ERROR "ENABLE_MARIADB is ON but MariaDB library not found")
    endif()
endif()

option(ENABLE_PG "Enable pg" OFF)
if (ENABLE_PG)
    include(cmake/pgsql.cmake)
    if (PGSQL_FOUND)
        message(STATUS "ENABLE_PG")
        include_directories(${PGSQL_INCLUDE_DIR})
        add_definitions(-DORMPP_ENABLE_PG)
    else()
        message(FATAL_ERROR "ENABLE_PG is ON but PostgreSQL library not found")
    endif()
endif()