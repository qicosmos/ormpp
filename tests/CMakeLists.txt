set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
project(test_ormpp)
add_executable(${PROJECT_NAME}
        test_ormpp.cpp
        async_mysql_smoke.cpp
        test_async_connection_pool.cpp
        async_pool_recovery.cpp
        test_adapter_timing.cpp
        main.cpp
        )

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# Link all detected database backends
if(PGSQL_FOUND)
        target_link_libraries(${PROJECT_NAME} ${PGSQL_LIBRARY})
endif()

if(ENABLE_PG_ASYNC)
        add_executable(test_postgresql_async async_postgresql.cpp main.cpp)
        target_link_libraries(test_postgresql_async PRIVATE ${PGSQL_LIBRARY} doctest)
        add_test(NAME test_postgresql_async COMMAND test_postgresql_async)
        set_tests_properties(test_postgresql_async PROPERTIES TIMEOUT 120)
endif()
if(MYSQL_FOUND)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
        if (MSVC)
                target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/MD> /bigobj)
        endif()
endif()
if(MARIADB_FOUND)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY})
endif()
target_link_libraries(${PROJECT_NAME} sqlite3)
target_link_libraries(${PROJECT_NAME} doctest)
if(ENABLE_MYSQL_ASYNC)
        target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)
        add_test(NAME ${PROJECT_NAME}_async_mysql_format
                COMMAND ${PROJECT_NAME} "--test-case=mysql async formats placeholders with question marks in values")
        add_test(NAME ${PROJECT_NAME}_async_mysql_smoke
                COMMAND ${PROJECT_NAME} "--test-case=mysql async smoke")
        add_test(NAME ${PROJECT_NAME}_async_connection_pool
                COMMAND ${PROJECT_NAME} --test-case=async_connection_pool:*)
        add_test(NAME ${PROJECT_NAME}_adapter_timing
                COMMAND ${PROJECT_NAME} "--test-case=asio async_simple adapter timing:*,asio async_simple safe adapter:*")
endif()
