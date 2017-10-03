# -*- cmake -*-

# - Find Google perftools
# Find the Google perftools includes and libraries
# This module defines
#  GOOGLE_PERFTOOLS_INCLUDE_DIR, where to find heap-profiler.h, etc.
#  GOOGLE_PERFTOOLS_FOUND, If false, do not try to use Google perftools.
# also defined for general use are
#  TCMALLOC_SHARED_LIB, path to tcmalloc's shared library
#  TCMALLOC_STATIC_LIB, path to tcmalloc's static library
#  PROFILER_SHARED_LIB, path to libprofiler's shared library
#  PROFILER_STATIC_LIB, path to libprofiler's static library

FIND_PATH(GOOGLE_PERFTOOLS_INCLUDE_DIR google/heap-profiler.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
FIND_LIBRARY(TCMALLOC_SHARED_LIB tcmalloc
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
FIND_LIBRARY(TCMALLOC_STATIC_LIB libtcmalloc.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
FIND_LIBRARY(PROFILER_SHARED_LIB profiler
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
FIND_LIBRARY(PROFILER_STATIC_LIB libprofiler.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GOOGLE_PERFTOOLS REQUIRED_VARS
  TCMALLOC_SHARED_LIB TCMALLOC_STATIC_LIB
  PROFILER_SHARED_LIB PROFILER_STATIC_LIB
  GOOGLE_PERFTOOLS_INCLUDE_DIR)
