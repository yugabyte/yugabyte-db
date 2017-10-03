# - Find GLOG (logging.h, libglog.a, libglog.so, and libglog.so.0)
# This module defines
#  GLOG_INCLUDE_DIR, directory containing headers
#  GLOG_SHARED_LIB, path to libglog's shared library
#  GLOG_STATIC_LIB, path to libglog's static library
#  GLOG_FOUND, whether glog has been found

find_path(GLOG_INCLUDE_DIR glog/logging.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GLOG_SHARED_LIB glog
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GLOG_STATIC_LIB libglog.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLOG REQUIRED_VARS
  GLOG_SHARED_LIB GLOG_STATIC_LIB GLOG_INCLUDE_DIR)
