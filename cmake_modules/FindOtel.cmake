# - Find GFLAGS (gflags.h, libgflags.a, libgflags.so, and libgflags.so.0)
# This module defines
#  GFLAGS_INCLUDE_DIR, directory containing headers
#  GFLAGS_SHARED_LIB, path to libgflags shared library
#  GFLAGS_STATIC_LIB, path to libgflags static library
#  GFLAGS_FOUND, whether gflags has been found

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
find_path(OTEL_INCLUDE_DIR opentelemetry/version.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_TRACE_SHARED_LIB libopentelemetry_trace.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_EXPORTER_MEMORY_SHARED_LIB libopentelemetry_exporter_in_memory.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_EXPORTER_OSTREAM_METRICS_SHARED_LIB libopentelemetry_exporter_in_memory.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_EXPORTER_OSTREAM_SPAN_SHARED_LIB libopentelemetry_exporter_ostream_span.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_COMMON_SHARED_LIB libopentelemetry_common.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_METRICS_SHARED_LIB libopentelemetry_metrics.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_RESOURCES_LIB libopentelemetry_resources.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_VERSION_SHARED_LIB libopentelemetry_version.so
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OPENTELEMETRY_CPP REQUIRED_VARS # correct way to use it?
  OTEL_TRACE_SHARED_LIB 
  OTEL_EXPORTER_MEMORY_SHARED_LIB 
  OTEL_EXPORTER_OSTREAM_METRICS_SHARED_LIB
  OTEL_EXPORTER_OSTREAM_SPAN_SHARED_LIB
  OTEL_COMMON_SHARED_LIB
  OTEL_METRICS_SHARED_LIB
  OTEL_RESOURCES_LIB
  OTEL_VERSION_SHARED_LIB
  OTEL_INCLUDE_DIR
)