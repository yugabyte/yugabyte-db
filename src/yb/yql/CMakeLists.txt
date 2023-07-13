# Copyright (c) YugaByte, Inc.
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

# TODO(dmitry): Remove next line after fixing #9279
set(CMAKE_CXX_FLAGS "${YB_CMAKE_CXX_EXTRA_FLAGS} ${CMAKE_CXX_FLAGS}")

# CQL service
add_subdirectory(cql/ql)
add_subdirectory(cql/cqlserver)

# REDIS service
add_subdirectory(redis/redisserver)

# API for Postgres to call.
add_subdirectory(pggate)

# Process wrapper, used by pgwrapper and ysql_conn_mgr_wrapper.
add_subdirectory(process_wrapper)

# Support for running a PostgreSQL server as a subprocess of the tserver process.
add_subdirectory(pgwrapper)

# Support for running a Ysql Connection Manager as a subprocess of the tserver process.
add_subdirectory(ysql_conn_mgr_wrapper)
