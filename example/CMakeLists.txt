project(ormpp_example)

set(ORMPP_EXAMPLE
    main.cpp
    )

add_executable(${PROJECT_NAME} ${ORMPP_EXAMPLE})

if(ENABLE_PG)
        target_link_libraries(${PROJECT_NAME} ${PGSQL_LIBRARY})
elseif(ENABLE_MYSQL)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
        if (MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
                set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/MD")
        endif()
elseif(ENABLE_MARIADB)
        target_link_libraries(${PROJECT_NAME} ${MARIADB_LIBRARY})
elseif(ENABLE_SQLITE3)
        target_link_libraries(${PROJECT_NAME} sqlite3)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION include)