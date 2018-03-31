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

# Postgres place all header files in one include directory and not together with source files.
include_directories(postgres/src/include)

add_library(ybpostgres_libpq
            pg_pqcomm.cc
            pg_pqformat.cc
            pg_stringinfo.cc
            pg_tuple.cc
            pg_type.cc)
target_link_libraries(ybpostgres_libpq
                      yb_util
                      yb_common)

add_library(pgsql-ybpostgres
            pg_instr.cc
            pg_port.cc
            pg_recv.cc
            pg_send.cc)
target_link_libraries(pgsql-ybpostgres
                      ybpostgres_libpq)
