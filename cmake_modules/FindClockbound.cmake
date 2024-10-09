# - Find clockbound (clockbound.h, libclockbound.a)
# This module defines
#  CLOCKBOUND_INCLUDE_DIR, directory containing headers
#  CLOCKBOUND_STATIC_LIB, path to clockbound's static library
#  CLOCKBOUND_FOUND, whether clockbound has been found

#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.
#

find_path(CLOCKBOUND_INCLUDE_DIR clockbound.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

# The AWS clockbound source exports both shared and static lib.
# Use a static lib to simplify packaging.
#
# Moreover, this lib is exported using a C interface. This means that
# the exported symbols in clockbound.h are not name mangled.
# Wrap clockbound.h in extern "C" before use in C++.
find_library(CLOCKBOUND_STATIC_LIB libclockbound.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CLOCKBOUND REQUIRED_VARS
  CLOCKBOUND_STATIC_LIB CLOCKBOUND_INCLUDE_DIR)
