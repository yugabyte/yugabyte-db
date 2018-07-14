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


set(POSTGRES_EXTRA_C_CXX_FLAGS "")
foreach(include_dir ${Boost_INCLUDE_DIRS})
  set(POSTGRES_EXTRA_C_CXX_FLAGS "${POSTGRES_EXTRA_C_CXX_FLAGS} -I${include_dir}")
endforeach(include_dir)

# Invoke our PostgreSQL build script.
#
# We add spaces before every "flags" parameter so that Python's argparse does not interpret them as
# its own flags.
add_custom_target(
  postgres
  COMMAND "${BUILD_SUPPORT_DIR}/build_postgres"
  --build_root "${YB_BUILD_ROOT}"

  --cflags     " ${POSTGRES_EXTRA_C_CXX_FLAGS} ${CMAKE_C_FLAGS}"
  --cxxflags   " ${POSTGRES_EXTRA_C_CXX_FLAGS} ${CMAKE_CXX_FLAGS}"
  --ldflags    " ${CMAKE_SHARED_LINKER_FLAGS}"
  --ldflags_ex " ${CMAKE_EXE_LINKER_FLAGS}"

  --compiler_type "$ENV{YB_COMPILER_TYPE}"
  DEPENDS yb_util yb_pggate)

# Make sure we rebuild postgres before the master/tserver libraries.
add_dependencies(master postgres)
add_dependencies(tserver postgres)
