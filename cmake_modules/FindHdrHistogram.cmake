#
# Copyright (c) Yugabytedb, Inc.
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

find_path(LIBHDR_HISTOGRAM_INCLUDE_DIR hdr/hdr_histogram.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(LIBHDR_HISTOGRAM_STATIC_LIB libhdr_histogram_static.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBHDR_HISTOGRAM REQUIRED_VARS
  LIBHDR_HISTOGRAM_STATIC_LIB LIBHDR_HISTOGRAM_INCLUDE_DIR)
