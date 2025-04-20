# - Find OpenSSL
# Find the native OpenSSL includes and libraries
# Uses the standard CMake FindOpenSSL module.
#
# It sets the following variables:
#  OpenSSL_FOUND       - True if OpenSSL found.
#  OpenSSL_INCLUDE_DIRS - where to find openssl/ssl.h, etc.
#  OpenSSL_LIBRARIES   - List of libraries when using OpenSSL.
#  OpenSSL_VERSION     - The version of OpenSSL found (x.y.z)

# Find OpenSSL using the standard CMake module.
# The REQUIRED keyword ensures that CMake will halt with an error if OpenSSL is not found.
find_package(OpenSSL REQUIRED)

# Optional: Add status messages if OpenSSL is found and find_package is not called quietly.
# The find_package command itself handles the error reporting when REQUIRED is used.
IF (OpenSSL_FOUND)
  MESSAGE(STATUS "Found OpenSSL: ${OpenSSL_LIBRARIES} (found version \"${OpenSSL_VERSION}\")")
  MESSAGE(STATUS "OpenSSL include directories: ${OpenSSL_INCLUDE_DIRS}")
ELSE(OpenSSL_FOUND)
  MESSAGE(FATAL_ERROR "OpenSSL not found. Please install OpenSSL")
ENDIF()

# The standard variables OpenSSL_INCLUDE_DIRS and OpenSSL_LIBRARIES
# are now available for use with target_include_directories and target_link_libraries.
