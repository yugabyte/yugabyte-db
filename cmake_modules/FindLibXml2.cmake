# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.
#

find_path(LIBXML2_INCLUDE_ROOT_DIR libxml2
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

set(LIBXML2_INCLUDE_DIR "${LIBXML2_INCLUDE_ROOT_DIR}/libxml2")
set(LIBXML2_INCLUDE_DIRS "${LIBXML2_INCLUDE_DIR}")

find_library(LIBXML2_SHARED_LIB NAMES xml2
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

set(LIBXML2_LIBRARY "${LIBXML2_SHARED_LIB}")
set(LIBXML2_LIBRARIES "${LIBXML2_SHARED_LIB}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibXml2 REQUIRED_VARS
  LIBXML2_SHARED_LIB LIBXML2_INCLUDE_DIR)
