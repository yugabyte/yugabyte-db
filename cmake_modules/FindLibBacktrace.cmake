# - Find LIBBACKTRACE (backtrace.h, libbacktrace.a, and libbacktrace.so)
# This module defines
#  LIBBACKTRACE_INCLUDE_DIR, directory containing headers
#  LIBBACKTRACE_SHARED_LIB, path to libbacktrace's shared library
#  LIBBACKTRACE_STATIC_LIB, path to libbacktrace's static library
#  LIBBACKTRACE_FOUND, whether libbacktrace has been found

find_path(LIBBACKTRACE_INCLUDE_DIR backtrace.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBBACKTRACE_SHARED_LIB backtrace
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBBACKTRACE_STATIC_LIB libbacktrace.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBBACKTRACE REQUIRED_VARS
  LIBBACKTRACE_SHARED_LIB LIBBACKTRACE_STATIC_LIB LIBBACKTRACE_INCLUDE_DIR)
