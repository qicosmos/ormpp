############################
# doctest
############################
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE doctest)

#############################
# sqlite3
#############################
if (ENABLE_SQLITE3)

#############################
# sqlcipher definitions
#############################
if(ENABLE_SQLITE3_CODEC)
add_definitions(
    -DSQLITE_EXTRA_INIT=sqlcipher_extra_init
    -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown
    -DSQLITE_THREADSAFE=2
    -DSQLITE_TEMP_STORE=2
    -DHAVE_STDINT_H
)
endif()

add_library(sqlite3 STATIC sqlite3/sqlite3.c)
target_include_directories(sqlite3 INTERFACE sqlite3)

if(ENABLE_SQLITE3_CODEC)
    target_include_directories(sqlite3 PUBLIC ${OpenSSL_INCLUDE_DIRS})
    target_link_libraries(sqlite3 PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif(ENABLE_SQLITE3_CODEC)

endif()