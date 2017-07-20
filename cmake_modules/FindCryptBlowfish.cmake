# Copyright (c) YugaByte, Inc.
#
# - Find CRYPT_BLOWFISH (crypt_blowfish/include/ow-crypt.h, libcrypt_blowfish.a)
# This module defines
#  CRYPT_BLOWFISH_INCLUDE_DIR, directory containing headers
#  CRYPT_BLOWFISH_STATIC_LIB, path to libcrypt_blowfish's static library
#  CRYPT_BLOWFISH_FOUND, whether crypt_blowfish has been found

find_path(CRYPT_BLOWFISH_INCLUDE_DIR crypt_blowfish/ow-crypt.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(CRYPT_BLOWFISH_STATIC_LIB libcrypt_blowfish.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CRYPT_BLOWFISH REQUIRED_VARS
  CRYPT_BLOWFISH_STATIC_LIB CRYPT_BLOWFISH_INCLUDE_DIR)
