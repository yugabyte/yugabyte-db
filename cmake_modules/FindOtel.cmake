# - Find OTEL (opentelemetry/version.h and OpenTelemetry C++ shared libraries)
# This module defines
#  OTEL_INCLUDE_DIR, directory containing headers
#  OTEL_TRACE_SHARED_LIB, path to opentelemetry_trace shared library
#  OTEL_EXPORTER_OTLP_HTTP_SHARED_LIB, path to opentelemetry_exporter_otlp_http shared library
#  OTEL_RESOURCES_SHARED_LIB, path to opentelemetry_resources shared library
#  OTEL_PROTO_SHARED_LIB, path to opentelemetry_proto shared library
#  OTEL_FOUND, whether OpenTelemetry C++ has been found

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
find_library(OTEL_TRACE_SHARED_LIB
  NAMES opentelemetry_trace
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_EXPORTER_OTLP_HTTP_SHARED_LIB
  NAMES opentelemetry_exporter_otlp_http
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_RESOURCES_SHARED_LIB
  NAMES opentelemetry_resources
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(OTEL_PROTO_SHARED_LIB
  NAMES opentelemetry_proto
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OTEL REQUIRED_VARS
  OTEL_TRACE_SHARED_LIB
  OTEL_EXPORTER_OTLP_HTTP_SHARED_LIB
  OTEL_RESOURCES_SHARED_LIB
  OTEL_PROTO_SHARED_LIB
  OTEL_INCLUDE_DIR
)
