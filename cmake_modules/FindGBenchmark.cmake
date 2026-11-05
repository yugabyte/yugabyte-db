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

# Finds google/benchmark (https://github.com/google/benchmark) in the third-party directory.
#
# This module defines
# GBENCHMARK_INCLUDE_DIR, where to find benchmark/benchmark.h
# GBENCHMARK_STATIC_LIB, path to libbenchmark.a
# GBENCHMARK_SHARED_LIB, path to the libbenchmark shared library

find_path(GBENCHMARK_INCLUDE_DIR benchmark/benchmark.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GBENCHMARK_STATIC_LIB libbenchmark.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(GBENCHMARK_SHARED_LIB benchmark
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GBENCHMARK REQUIRED_VARS
  GBENCHMARK_STATIC_LIB GBENCHMARK_SHARED_LIB GBENCHMARK_INCLUDE_DIR)
