# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
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
