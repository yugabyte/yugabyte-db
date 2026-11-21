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

# The master: the Rust master and its C++ host. See src/yb/rust/master/docs/cpp-host.md.

yb_rust_cxxgen(MASTER_HOST_CXXGEN_SRCS
  master/server/src/ffi.rs)

add_library(yb_master_host_obj OBJECT
  master_host.cc
  sys_catalog_tap.cc
  ycql_virtual_tables.cc
  ${MASTER_HOST_CXXGEN_SRCS})
target_link_libraries(yb_master_host_obj PUBLIC yb_rust_bridge_obj)
# Its sources include the master's generated protobuf headers.
add_dependencies(yb_master_host_obj master)

yb_add_rust_staticlib(yb_master_rs master-server)

# In LTO builds this is the base of the yb-server executable (the top-level CMakeLists.txt), which
# acts as yb-master or yb-tserver by argv[0].
add_executable("${YB_MASTER_DYNAMIC_EXE_NAME}" master_host_main.cc)
set(YB_MASTER_EXE_DEPS
    yb_master_host_obj
    yb_rust_bridge_obj
    yb_master_rs
    master
    openssl
    ${YB_BASE_LIBS})
if(YB_LTO_ENABLED)
  # The LTO binary statically merges docdb, which calls the ybgate C API in yb_pgbackend. yb_docdb
  # links yb_pgbackend PRIVATE, so (unlike the dynamic build, which resolves it at load time via
  # libyb_docdb.so's DT_NEEDED) the flattened LTO link needs yb_pgbackend on the executable link
  # line for lto.py to pull in its objects.
  list(APPEND YB_MASTER_EXE_DEPS tserver_main_impl yb_pgbackend)
endif()
target_link_libraries("${YB_MASTER_DYNAMIC_EXE_NAME}" ${YB_MASTER_EXE_DEPS})

# Generate master_flags.xml
set(MASTER_FLAGS_METADATA_FILE ${YB_BUILD_ROOT}/master_flags.xml)
add_custom_command(TARGET "${YB_MASTER_DYNAMIC_EXE_NAME}" POST_BUILD
  COMMAND "${BUILD_SUPPORT_DIR}/gen_flags_metadata_wrapper"
    "--program_name" "${YB_MASTER_DYNAMIC_EXE_NAME}"
    "--output_file_path" "${MASTER_FLAGS_METADATA_FILE}"
    "--dynamically_linked_exe_suffix=${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}"
  BYPRODUCTS "${MASTER_FLAGS_METADATA_FILE}")
# yb-master must keep every gflag of the C++ master it replaced (build-support/check_master_flags.py).
add_custom_command(TARGET "${YB_MASTER_DYNAMIC_EXE_NAME}" POST_BUILD
  COMMAND "${BUILD_SUPPORT_DIR}/check_master_flags.py"
    "--baseline" "${YB_SRC_ROOT}/src/yb/master/master_flags_baseline.txt"
    "--binary" "$<TARGET_FILE:${YB_MASTER_DYNAMIC_EXE_NAME}>")

# Generate auto_flags.json
set(AUTO_FLAGS_FILE ${YB_BUILD_ROOT}/auto_flags.json)
list(APPEND GEN_AUTO_FLAGS_COMMAND "${BUILD_SUPPORT_DIR}/gen_auto_flags"
  "--program_list" "${YB_MASTER_DYNAMIC_EXE_NAME},${YB_TSERVER_DYNAMIC_EXE_NAME}"
  "--dynamically_linked_exe_suffix=${YB_DYNAMICALLY_LINKED_EXE_SUFFIX}"
  "--output_file_path" "${AUTO_FLAGS_FILE}"
  "--previous_auto_flags_file" "${YB_SRC_ROOT}/src/yb/previous_auto_flags.json")
add_custom_target(
  gen_auto_flags_json ALL
  COMMAND ${GEN_AUTO_FLAGS_COMMAND}
  BYPRODUCTS "${AUTO_FLAGS_FILE}"
  DEPENDS "${YB_MASTER_DYNAMIC_EXE_NAME}" "${YB_TSERVER_DYNAMIC_EXE_NAME}")

# The Rust master as a library, for in-process test clusters (embedded_master.h). Only the
# integration-tests library links it, so no executable gets two Rust static libraries.
ADD_YB_LIBRARY(yb_master_rs_embedded
  SRCS embedded_master.cc
  DEPS yb_master_host_obj yb_rust_bridge_obj yb_master_rs master openssl ${YB_BASE_LIBS})

# Checks the Rust protobuf runtime against rows dumped by `sys-catalog-tool dump-rows`.
yb_add_rust_tool(master-conformance)

# Fake tservers for loading a master (src/yb/rust/master/docs/testing.md). Its own executable with its
# own Rust static library.
yb_rust_cxxgen(TSERVER_SIM_CXXGEN_SRCS
  master/tools/tserver-sim-main/src/ffi.rs)
yb_add_rust_staticlib(yb_master_tserver_sim_rs master-tserver-sim-main)
add_executable(master-tserver-sim
  tserver_sim_main.cc
  tserver_sim_host.cc
  ${TSERVER_SIM_CXXGEN_SRCS})
target_link_libraries(master-tserver-sim
  yb_rust_bridge_obj
  yb_master_tserver_sim_rs
  server_process
  ${YB_BASE_LIBS})
