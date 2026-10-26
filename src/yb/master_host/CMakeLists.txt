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

# The C++ host of the Rust master (yb-master-rs). See src/rust/master/docs/cpp-host.md.

yb_rust_cxxgen(MASTER_HOST_CXXGEN_SRCS
  master/master/src/ffi.rs)

add_library(yb_master_host_obj OBJECT
  ${MASTER_HOST_CXXGEN_SRCS})
target_link_libraries(yb_master_host_obj PUBLIC yb_rust_bridge_obj)

yb_add_rust_staticlib(yb_master_rs master)

add_executable(yb-master-rs${YB_DYNAMICALLY_LINKED_EXE_SUFFIX} master_host_main.cc)
set(YB_MASTER_RS_DEPS
    yb_master_host_obj
    yb_rust_bridge_obj
    yb_master_rs
    master
    ${YB_BASE_LIBS})
if(YB_LTO_ENABLED)
  list(APPEND YB_MASTER_RS_DEPS tserver_main_impl yb_pgbackend)
endif()
target_link_libraries(yb-master-rs${YB_DYNAMICALLY_LINKED_EXE_SUFFIX} ${YB_MASTER_RS_DEPS})

# Built only on request until the LTO whole-program link is validated with a Rust static library.
yb_add_lto_target(yb-master-rs${YB_DYNAMICALLY_LINKED_EXE_SUFFIX} yb-master-rs "" EXCLUDE_FROM_ALL)

