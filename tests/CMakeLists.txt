set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
project(test_ormpp)
add_executable(${PROJECT_NAME}
        test_ormpp.cpp
        main.cpp
        )

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# Link all detected database backends
if(PGSQL_FOUND)
        target_link_libraries(${PROJECT_NAME} ${PGSQL_LIBRARY})
endif()
if(MYSQL_FOUND)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
        if (MSVC)
                target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/MD>)
        endif()
endif()
if(MARIADB_FOUND)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY})
endif()
target_link_libraries(${PROJECT_NAME} sqlite3)
target_link_libraries(${PROJECT_NAME} doctest)
