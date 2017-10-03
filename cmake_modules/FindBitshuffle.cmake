# - Find Bitshuffle (bitshuffle.h, bitshuffle.a)
# This module defines
#  BITSHUFFLE_INCLUDE_DIR, directory containing headers
#  BITSHUFFLE_STATIC_LIB, path to bitshuffle's static library
#  BITSHUFFLE_FOUND, whether bitshuffle has been found

find_path(BITSHUFFLE_INCLUDE_DIR bitshuffle.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

find_library(BITSHUFFLE_STATIC_LIB bitshuffle.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BITSHUFFLE REQUIRED_VARS
  BITSHUFFLE_STATIC_LIB BITSHUFFLE_INCLUDE_DIR)
