#############################
# doctest
#############################
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE doctest)

#############################
# sqlite3
#############################
if (ENABLE_SQLITE3)
add_library(sqlite3 STATIC sqlite3/sqlite3.c)
target_include_directories(sqlite3 INTERFACE sqlite3)
endif()