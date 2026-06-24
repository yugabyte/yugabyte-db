# Copyright (c) YugabyteDB, Inc.
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

set(YB_PCH_PATH ../)

# We need access to the generated pg_type_d.h header from this directory.
include_directories("${YB_BUILD_ROOT}/postgres_build/src/backend")

set(PGGATE_UTIL_SRCS
    pg_wire.cc
    pg_doc_data.cc
    pg_tuple.cc
    ybc_guc.cc
    ybc_util.cc
    ybc-internal.cc
    )

set(PGGATE_UTIL_LIBS
    yb_util
    yb_common
    yb_ash)

ADD_YB_LIBRARY(yb_pggate_util
               SRCS ${PGGATE_UTIL_SRCS}
               DEPS ${PGGATE_UTIL_LIBS})

add_dependencies(yb_pggate_util postgres_genbki)

# Allow undefined symbols for yb_dockv functions to avoid circular dependency
# yb_dockv depends on yb_pggate_util, so we can't add it as a dependency here
# The symbols will be resolved when both libraries are linked into executables
if(NOT APPLE)
  target_link_options(yb_pggate_util PRIVATE
    "LINKER:--allow-shlib-undefined"
    "LINKER:--unresolved-symbols=ignore-all"
  )
endif()

ADD_YB_LIBRARY(yb_pgresult_util
  SRCS ybc_pgresult_util.cc
  DEPS yb_common_proto
  NONLINK_DEPS postgres_genbki)

target_include_directories(yb_pgresult_util PRIVATE
  ${YB_BUILD_ROOT}/postgres_build/src/include
  ${YB_BUILD_ROOT}/postgres_build/src/interfaces/libpq)

# Allow undefined symbols for PostgreSQL libpq functions (PQ*, YbPQ*)
# These will be resolved when linked into executables that link against libpq
if(NOT APPLE)
  target_link_options(yb_pgresult_util PRIVATE
    "LINKER:--allow-shlib-undefined"
    "LINKER:--unresolved-symbols=ignore-all"
  )
endif()

