set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
project(test_ormpp)
add_executable(${PROJECT_NAME}
        test_ormpp.cpp
        main.cpp
        )

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if (ENABLE_MYSQL)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY} doctest)
        if (MSVC)
        set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MD")
        endif()
endif()

if (ENABLE_MARIADB)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY})
endif()

if (ENABLE_SQLITE3)
        target_link_libraries(${PROJECT_NAME} sqlite3 doctest)
endif()

if (ENABLE_PG)
        target_link_libraries(${PROJECT_NAME} pg doctest)
endif()