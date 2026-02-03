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

find_path(EIGEN_INCLUDE_ROOT_DIR Eigen
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
set(EIGEN_INCLUDE_DIR "${EIGEN_INCLUDE_ROOT_DIR}/Eigen")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EIGEN REQUIRED_VARS
  EIGEN_INCLUDE_DIR)
