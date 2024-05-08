# - Find mysqlclient
# Find the native MySQL includes and library
#
#  MYSQL_INCLUDE_DIR - where to find mysql.h, etc.
#  MYSQL_LIBRARIES   - List of libraries when using MySQL.
#  MYSQL_FOUND       - True if MySQL found.

IF (MYSQL_INCLUDE_DIR)
  # Already in cache, be silent
  SET(MYSQL_FIND_QUIETLY TRUE)
ENDIF (MYSQL_INCLUDE_DIR)

IF (WIN32)
  FIND_PATH(MYSQL_INCLUDE_DIR mysql.h
    $ENV{PROGRAMFILES}/MySQL/*/include
    $ENV{SYSTEMDRIVE}/MySQL/*/include)
ELSEIF (LINUX)
  FIND_PATH(MYSQL_INCLUDE_DIR mysql.h
    /usr/local/include/mysql
    /usr/include/mysql)
ELSEIF (APPLE)
  FIND_PATH(MYSQL_INCLUDE_DIR mysql.h
  /opt/homebrew/include/mysql
  /opt/homebrew/opt/mysql@8.0/include
  /opt/homebrew/Cellar/mysql@8.0/*/include/mysql)
ENDIF()

SET(MYSQL_NAMES mysqlclient)
IF (WIN32)
  FIND_LIBRARY(MYSQL_LIBRARY
    NAMES ${MYSQL_NAMES}
    PATHS $ENV{PROGRAMFILES}/MySQL/*/lib 
    $ENV{SYSTEMDRIVE}/MySQL/*/lib
    PATH_SUFFIXES mysql)
ELSEIF (LINUX)
  FIND_LIBRARY(MYSQL_LIBRARY
    NAMES ${MYSQL_NAMES}
    PATHS /usr/lib 
    /usr/local/lib
    PATH_SUFFIXES mysql)
ELSEIF (APPLE)
  FIND_LIBRARY(MYSQL_LIBRARY
    NAMES ${MYSQL_NAMES}
    PATHS /opt/homebrew/lib
    /opt/homebrew/opt/mysql@8.0/lib
    /opt/homebrew/Cellar/mysql@8.0/*/lib
    PATH_SUFFIXES mysql)
ENDIF()

IF (MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
  SET(MYSQL_FOUND TRUE)
  SET(MYSQL_LIBRARIES ${MYSQL_LIBRARY})
ELSE (MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
  SET(MYSQL_FOUND FALSE)
  SET(MYSQL_LIBRARIES)
ENDIF (MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)

IF (MYSQL_FOUND)
  IF (NOT MYSQL_FIND_QUIETLY)
    MESSAGE(STATUS "Found MySQL: ${MYSQL_LIBRARY}")
  ENDIF (NOT MYSQL_FIND_QUIETLY)
ELSE (MYSQL_FOUND)
  IF (MYSQL_FIND_REQUIRED)
    MESSAGE(STATUS "Looked for MySQL libraries named ${MYSQL_NAMES}.")
    MESSAGE(FATAL_ERROR "Could NOT find MySQL library")
  ENDIF (MYSQL_FIND_REQUIRED)
ENDIF (MYSQL_FOUND)

MARK_AS_ADVANCED(MYSQL_LIBRARY MYSQL_INCLUDE_DIR)