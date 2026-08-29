# Compile Standard
option(ENABLE_CXX26_REFLECTION
       "Build ormpp with experimental C++26 static reflection" OFF)
if(ENABLE_CXX26_REFLECTION)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
       CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        message(FATAL_ERROR
                "ENABLE_CXX26_REFLECTION currently requires GCC 16 or newer")
    endif()
    set(CMAKE_CXX_STANDARD 26)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-freflection>)
    message(STATUS "Experimental C++26 reflection tests: enabled")
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "CXX Standard: ${CMAKE_CXX_STANDARD}")

# Build Type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# libc++ or libstdc++&clang
option(BUILD_WITH_LIBCXX "Build with libc++" OFF)
if(BUILD_WITH_LIBCXX AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    message(STATUS "Build with libc++")
else()
    message(STATUS "Build with libstdc++")
endif()
