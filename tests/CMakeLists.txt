set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
project(test_ormpp)
add_executable(${PROJECT_NAME}
        test_ormpp.cpp
        main.cpp
        )

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if(ENABLE_PG)
        target_link_libraries(${PROJECT_NAME} ${PGSQL_LIBRARY} doctest)
elseif(ENABLE_MYSQL)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY} doctest)
elseif(ENABLE_MARIADB)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY} doctest)
elseif(ENABLE_SQLITE3)
        target_link_libraries(${PROJECT_NAME} sqlite3 doctest)
endif()