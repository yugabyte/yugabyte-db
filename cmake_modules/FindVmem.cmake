# - Find VMEM (libvmem.h, libvmem.so)
# This module defines
#  VMEM_INCLUDE_DIR, directory containing headers
#  VMEM_SHARED_LIB, path to vmem's shared library
#  VMEM_STATIC_LIB, path to vmem's static library
#  VMEM_FOUND, whether libvmem has been found

find_path(VMEM_INCLUDE_DIR libvmem.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(VMEM_SHARED_LIB vmem
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(VMEM_STATIC_LIB libvmem.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VMEM REQUIRED_VARS
  VMEM_SHARED_LIB VMEM_STATIC_LIB VMEM_INCLUDE_DIR)
