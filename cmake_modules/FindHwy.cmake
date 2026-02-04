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

find_path(HWY_INCLUDE_ROOT_DIR libhwy
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
set(HWY_INCLUDE_DIR "${HWY_INCLUDE_ROOT_DIR}/libhwy")
find_library(HWY_STATIC_LIB libhwy.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HWY REQUIRED_VARS
  HWY_STATIC_LIB HWY_INCLUDE_DIR)


find_path(HWY_CONTRIB_INCLUDE_ROOT_DIR libhwy_contrib
# make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(HWY_CONTRIB_STATIC_LIB libhwy_contrib.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HWY_CONTRIB REQUIRED_VARS
  HWY_CONTRIB_STATIC_LIB)
