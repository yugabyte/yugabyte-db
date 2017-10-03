# - Find LIBEV (ev++.h, libev.a, and libev.so)
# This module defines
#  LIBEV_INCLUDE_DIR, directory containing headers
#  LIBEV_SHARED_LIB, path to libev's shared library
#  LIBEV_STATIC_LIB, path to libev's static library
#  LIBEV_FOUND, whether libev has been found

find_path(LIBEV_INCLUDE_DIR ev++.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBEV_SHARED_LIB ev
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBEV_STATIC_LIB libev.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBEV REQUIRED_VARS
  LIBEV_SHARED_LIB LIBEV_STATIC_LIB LIBEV_INCLUDE_DIR)
