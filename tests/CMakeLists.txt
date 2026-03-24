set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
project(test_ormpp)
add_executable(${PROJECT_NAME}
        test_ormpp.cpp
        main.cpp
        )

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# Link all enabled database backends
if(ENABLE_PG)
        target_link_libraries(${PROJECT_NAME} ${PGSQL_LIBRARY})
endif()
if(ENABLE_MYSQL)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
        if (MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
                set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MD")
        endif()
endif()
if(ENABLE_MARIADB)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY})
endif()
if(ENABLE_SQLITE3)
        target_link_libraries(${PROJECT_NAME} sqlite3)
endif()
target_link_libraries(${PROJECT_NAME} doctest)
