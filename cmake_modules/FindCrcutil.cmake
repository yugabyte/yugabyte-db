# - Find CRCUTIL (crcutil/include.h, libcrcutil.a)
# This module defines
#  CRCUTIL_INCLUDE_DIR, directory containing headers
#  CRCUTIL_SHARED_LIB, path to libcrcutil's shared library
#  CRCUTIL_STATIC_LIB, path to libcrcutil's static library
#  CRCUTIL_FOUND, whether crcutil has been found

#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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
