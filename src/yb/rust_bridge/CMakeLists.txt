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

# C++ side of the cxx bridges shared by every executable that hosts Rust code. See
# src/rust/docs/cpp-host.md.

# Shared #[cxx::bridge] files (paths under src/rust); a component's own bridges are generated in
# its directory (e.g. src/yb/master_host). This call also writes rust/cxx.h.
yb_rust_cxxgen(RUST_BRIDGE_CXXGEN_SRCS WITH_CXX_HEADER
  common/yb-cpp-host/src/ffi/flags.rs
  common/yb-cpp-host/src/ffi/log.rs
  common/yb-cpp-host/src/ffi/rpc.rs)

# An object library rather than a shared one: bridge code and the Rust static library call each
# other, so both must end up in the same executable.
add_library(yb_rust_bridge_obj OBJECT
  flags_bridge.cc
  log_bridge.cc
  rpc_bridge.cc
  ${RUST_BRIDGE_CXXGEN_SRCS})
target_include_directories(yb_rust_bridge_obj PUBLIC "${YB_RUST_CXXGEN_DIR}")

# rustfmt, clippy and the Rust unit tests: ./yb_build.sh <type> --target rust_checks
# Crates that only link inside a CMake-built executable are covered by C++ tests instead.
add_custom_target(rust_checks
  COMMAND ${YB_RUST_CARGO_ENV} "${YB_CARGO_EXECUTABLE}" fmt --all --check
  COMMAND ${YB_RUST_CARGO_ENV} "${YB_CARGO_EXECUTABLE}" clippy --locked --workspace --all-targets
          -- -D warnings
  COMMAND ${YB_RUST_CARGO_ENV} "${YB_CARGO_EXECUTABLE}" test --locked --workspace
          ${YB_RUST_TEST_EXCLUDES}
  WORKING_DIRECTORY "${YB_RUST_SRC_DIR}"
  USES_TERMINAL)
