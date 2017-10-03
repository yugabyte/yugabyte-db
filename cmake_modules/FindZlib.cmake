# - Find ZLIB (zlib.h, libz.a, libz.so, and libz.so.1)
# This module defines
#  ZLIB_INCLUDE_DIR, directory containing headers
#  ZLIB_SHARED_LIB, path to libz's shared library
#  ZLIB_STATIC_LIB, path to libz's static library
#  ZLIB_FOUND, whether zlib has been found

find_path(ZLIB_INCLUDE_DIR zlib.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(ZLIB_SHARED_LIB z
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(ZLIB_STATIC_LIB libz.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZLIB REQUIRED_VARS
  ZLIB_SHARED_LIB ZLIB_STATIC_LIB ZLIB_INCLUDE_DIR)
