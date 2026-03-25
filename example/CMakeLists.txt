project(ormpp_example)

set(ORMPP_EXAMPLE
    main.cpp
    )

add_executable(${PROJECT_NAME} ${ORMPP_EXAMPLE})

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

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
