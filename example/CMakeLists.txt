project(ormpp_example)

set(ORMPP_EXAMPLE
    main.cpp
    )

add_executable(${PROJECT_NAME} ${ORMPP_EXAMPLE})

if (ENABLE_MYSQL)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_LIBRARY})
endif()

if (ENABLE_SQLITE3)
        target_link_libraries(${PROJECT_NAME} sqlite3)
endif()

if (ENABLE_PG)
        target_link_libraries(${PROJECT_NAME} pg)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION include)