# - Find GFLAGS (gflags.h, libgflags.a, libgflags.so, and libgflags.so.0)
# This module defines
#  GFLAGS_INCLUDE_DIR, directory containing headers
#  GFLAGS_SHARED_LIB, path to libgflags shared library
#  GFLAGS_STATIC_LIB, path to libgflags static library
#  GFLAGS_FOUND, whether gflags has been found

find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GFLAGS_SHARED_LIB gflags
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GFLAGS_STATIC_LIB libgflags.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GFLAGS REQUIRED_VARS
  GFLAGS_SHARED_LIB GFLAGS_STATIC_LIB GFLAGS_INCLUDE_DIR)
