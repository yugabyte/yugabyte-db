# - Find GLOG (logging.h, libglog.a, libglog.so, and libglog.so.0)
# This module defines
#  GLOG_INCLUDE_DIR, directory containing headers
#  GLOG_SHARED_LIB, path to libglog's shared library
#  GLOG_STATIC_LIB, path to libglog's static library
#  GLOG_FOUND, whether glog has been found

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
