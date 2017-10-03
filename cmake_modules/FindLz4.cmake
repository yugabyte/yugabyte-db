# - Find LZ4 (lz4.h, liblz4.a)
# This module defines
#  LZ4_INCLUDE_DIR, directory containing headers
#  LZ4_STATIC_LIB, path to liblz4's static library
#  LZ4_FOUND, whether lz4 has been found

find_path(LZ4_INCLUDE_DIR lz4.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LZ4_STATIC_LIB liblz4.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LZ4 REQUIRED_VARS
  LZ4_STATIC_LIB LZ4_INCLUDE_DIR)
