# - Find OpenSSL
# Find the native OpenSSL includes and libraries
#
#  OPENSSL_INCLUDE_DIR - where to find openssl/ssl.h, etc.
#  OPENSSL_LIBRARIES   - List of libraries when using OpenSSL.
#  OPENSSL_FOUND       - True if OpenSSL found.

IF (OPENSSL_INCLUDE_DIR)
  # Already in cache, be silent
  SET(OPENSSL_FIND_QUIETLY TRUE)
ENDIF (OPENSSL_INCLUDE_DIR)

IF (WIN32)
  FIND_PATH(OPENSSL_INCLUDE_DIR openssl/ssl.h
    $ENV{PROGRAMFILES}/OpenSSL/*/include
    $ENV{PROGRAMFILES}/OpenSSL-Win32/include
    $ENV{PROGRAMFILES}/OpenSSL-Win64/include
    $ENV{SYSTEMDRIVE}/OpenSSL/*/include)
ELSEIF (LINUX)
  FIND_PATH(OPENSSL_INCLUDE_DIR openssl/ssl.h
    /usr/local/include
    /usr/include)
ELSEIF (APPLE)
  FIND_PATH(OPENSSL_INCLUDE_DIR openssl/ssl.h
    /opt/homebrew/include
    /opt/homebrew/opt/openssl/include
    /opt/homebrew/Cellar/openssl/*/include)
ELSE (WIN32)
  FIND_PATH(OPENSSL_INCLUDE_DIR openssl/ssl.h
    /opt/homebrew/include
    /opt/homebrew/opt/openssl/include
    /opt/homebrew/Cellar/openssl/*/include
    /usr/local/include
    /usr/include)
ENDIF()

SET(SSL_NAMES ssl libssl)
SET(CRYPTO_NAMES crypto libcrypto)

IF (WIN32)
  FIND_LIBRARY(SSL_LIBRARY
    NAMES ${SSL_NAMES}
    PATHS $ENV{PROGRAMFILES}/OpenSSL/*/lib
    $ENV{PROGRAMFILES}/OpenSSL-Win64/lib/VC/x64/MD
    $ENV{PROGRAMFILES}/OpenSSL-Win32/lib/VC/x86/MD
    $ENV{SYSTEMDRIVE}/OpenSSL/*/lib)
  FIND_LIBRARY(CRYPTO_LIBRARY
    NAMES ${CRYPTO_NAMES}
    PATHS $ENV{PROGRAMFILES}/OpenSSL/*/lib
    $ENV{PROGRAMFILES}/OpenSSL-Win64/lib/VC/x64/MD
    $ENV{PROGRAMFILES}/OpenSSL-Win32/lib/VC/x86/MD
    $ENV{SYSTEMDRIVE}/OpenSSL/*/lib)
ELSEIF (LINUX)
  FIND_LIBRARY(SSL_LIBRARY
    NAMES ${SSL_NAMES}
    PATHS /usr/lib 
    /usr/local/lib)
  FIND_LIBRARY(CRYPTO_LIBRARY
    NAMES ${CRYPTO_NAMES}
    PATHS /usr/lib 
    /usr/local/lib)
ELSEIF (APPLE)
  FIND_LIBRARY(SSL_LIBRARY
    NAMES ${SSL_NAMES}
    PATHS /opt/homebrew/lib
    /opt/homebrew/opt/openssl/lib
    /opt/homebrew/Cellar/openssl/*/lib)
  FIND_LIBRARY(CRYPTO_LIBRARY
    NAMES ${CRYPTO_NAMES}
    PATHS /opt/homebrew/lib
    /opt/homebrew/opt/openssl/lib
    /opt/homebrew/Cellar/openssl/*/lib)
ELSE (WIN32)
  FIND_LIBRARY(SSL_LIBRARY
    NAMES ${SSL_NAMES}
    PATHS /usr/lib 
    /usr/local/lib
    /opt/homebrew/lib
    /opt/homebrew/opt/openssl/lib
    /opt/homebrew/Cellar/openssl/*/lib)
  FIND_LIBRARY(CRYPTO_LIBRARY
    NAMES ${CRYPTO_NAMES}
    PATHS /usr/lib 
    /usr/local/lib
    /opt/homebrew/lib
    /opt/homebrew/opt/openssl/lib
    /opt/homebrew/Cellar/openssl/*/lib)
ENDIF()

IF (OPENSSL_INCLUDE_DIR AND SSL_LIBRARY AND CRYPTO_LIBRARY)
  SET(OPENSSL_FOUND TRUE)
  SET(OPENSSL_LIBRARIES ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
ELSE (OPENSSL_INCLUDE_DIR AND SSL_LIBRARY AND CRYPTO_LIBRARY)
  SET(OPENSSL_FOUND FALSE)
  SET(OPENSSL_LIBRARIES)
ENDIF (OPENSSL_INCLUDE_DIR AND SSL_LIBRARY AND CRYPTO_LIBRARY)

IF (OPENSSL_FOUND)
  IF (NOT OPENSSL_FIND_QUIETLY)
    MESSAGE(STATUS "Found OpenSSL: ${SSL_LIBRARY}, ${CRYPTO_LIBRARY}")
  ENDIF (NOT OPENSSL_FIND_QUIETLY)
ELSE (OPENSSL_FOUND)
  IF (OPENSSL_FIND_REQUIRED)
    MESSAGE(STATUS "Looked for OpenSSL libraries named ${SSL_NAMES} and ${CRYPTO_NAMES}.")
    MESSAGE(FATAL_ERROR "Could NOT find OpenSSL libraries")
  ENDIF (OPENSSL_FIND_REQUIRED)
ENDIF (OPENSSL_FOUND)

MARK_AS_ADVANCED(SSL_LIBRARY OPENSSL_INCLUDE_DIR)
