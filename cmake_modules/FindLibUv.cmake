# - Find LIBUV (uv.h, libuv_a.a, and libuv.so)
# This module defines
#  LIBUV_INCLUDE_DIR, directory containing headers
#  LIBUV_SHARED_LIB, path to libuv's shared library
#  LIBUV_STATIC_LIB, path to libuv's static library
#  LIBUV_FOUND, whether libev has been found

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
find_path(LIBUV_INCLUDE_DIR uv.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBUV_SHARED_LIB uv
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBUV_STATIC_LIB libuv_a.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBUV REQUIRED_VARS
  LIBUV_SHARED_LIB LIBUV_STATIC_LIB LIBUV_INCLUDE_DIR)
