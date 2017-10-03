# - Find CRCUTIL (crcutil/include.h, libcrcutil.a)
# This module defines
#  CRCUTIL_INCLUDE_DIR, directory containing headers
#  CRCUTIL_SHARED_LIB, path to libcrcutil's shared library
#  CRCUTIL_STATIC_LIB, path to libcrcutil's static library
#  CRCUTIL_FOUND, whether crcutil has been found

find_path(CRCUTIL_INCLUDE_DIR crcutil/interface.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(CRCUTIL_SHARED_LIB crcutil
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(CRCUTIL_STATIC_LIB libcrcutil.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CRCUTIL REQUIRED_VARS
  CRCUTIL_SHARED_LIB CRCUTIL_STATIC_LIB CRCUTIL_INCLUDE_DIR)
