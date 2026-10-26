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

# Builds the Cargo workspace at src/rust (Rust code in YugabyteDB, starting with the master) as
# part of the CMake build.
# See src/rust/docs/build.md for the overall layout.
#
# CMake owns every final link (sanitizer runtimes, tcmalloc, rpaths, LTO), so Cargo only produces
# static libraries and build-time tools. Each executable links exactly one Rust static library:
# two would each carry a copy of the Rust standard library.

# Included once, from the top-level CMakeLists.txt, so its variables reach every directory.
include_guard(GLOBAL)

set(YB_RUST_SRC_DIR "${YB_SRC_ROOT}/src/rust")
set(YB_RUST_TARGET_DIR "${YB_BUILD_ROOT}/rust/target")
set(YB_RUST_CXXGEN_DIR "${YB_BUILD_ROOT}/rust/cxxgen")

file(READ "${YB_RUST_SRC_DIR}/rust-toolchain.toml" _yb_rust_toolchain_toml)
if(NOT _yb_rust_toolchain_toml MATCHES "channel = \"([0-9.]+)\"")
  message(FATAL_ERROR "Could not find the toolchain channel in src/rust/rust-toolchain.toml")
endif()
set(YB_RUST_TOOLCHAIN_VERSION "${CMAKE_MATCH_1}")

find_program(YB_CARGO_EXECUTABLE cargo
             HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
if(NOT YB_CARGO_EXECUTABLE)
  message(FATAL_ERROR
          "cargo not found. The Rust yb-master needs Rust ${YB_RUST_TOOLCHAIN_VERSION} "
          "(see src/rust/docs/build.md), or pass --skip-rust to yb_build.sh.")
endif()

# rustup reads rust-toolchain.toml when cargo runs from inside src/rust; without rustup the
# installed cargo must already be the pinned version.
execute_process(
  COMMAND "${YB_CARGO_EXECUTABLE}" --version
  WORKING_DIRECTORY "${YB_RUST_SRC_DIR}"
  OUTPUT_VARIABLE _yb_cargo_version_output
  RESULT_VARIABLE _yb_cargo_version_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _yb_cargo_version_result EQUAL 0 OR
   NOT _yb_cargo_version_output MATCHES "^cargo ${YB_RUST_TOOLCHAIN_VERSION} ")
  message(FATAL_ERROR
          "Expected cargo ${YB_RUST_TOOLCHAIN_VERSION} (pinned in src/rust/rust-toolchain.toml), "
          "got: '${_yb_cargo_version_output}'")
endif()
message("Using ${_yb_cargo_version_output} from ${YB_CARGO_EXECUTABLE}")

if("${YB_BUILD_TYPE}" STREQUAL "debug")
  set(YB_RUST_PROFILE "dev")
  set(YB_RUST_PROFILE_DIR "debug")
else()
  set(YB_RUST_PROFILE "release")
  set(YB_RUST_PROFILE_DIR "release")
endif()

# The only C++ Cargo compiles is the cxx runtime (cxx.cc, built by the cxx crate's build script).
# It must use the same compiler and standard library (libc++) as the rest of the build, because
# rust::String and friends cross into C++ code compiled by CMake. Inherited CFLAGS/CXXFLAGS are
# dropped for the same reason as in the pgrx build (pgrx-common.mk): they contain -Werror and
# include paths that break *-sys crates and make Cargo rebuild them on every flag change.
if(DEFINED ENV{YB_LLVM_TOOLCHAIN_DIR} AND NOT "$ENV{YB_LLVM_TOOLCHAIN_DIR}" STREQUAL "")
  set(_yb_rust_cc "$ENV{YB_LLVM_TOOLCHAIN_DIR}/bin/clang")
  set(_yb_rust_cxx "$ENV{YB_LLVM_TOOLCHAIN_DIR}/bin/clang++")
else()
  set(_yb_rust_cc "${CMAKE_C_COMPILER}")
  set(_yb_rust_cxx "${CMAKE_CXX_COMPILER}")
endif()
set(_yb_rust_cxxflags "-fPIC -O2")
if(DEFINED LIBCXX_INCLUDE_DIR)
  string(APPEND _yb_rust_cxxflags " -stdlib=libc++ -nostdinc++ -isystem ${LIBCXX_INCLUDE_DIR}")
endif()

set(YB_RUST_CARGO_ENV
    env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS -u RUSTFLAGS -u PKG_CONFIG_PATH
    "CC=${_yb_rust_cc}"
    "CXX=${_yb_rust_cxx}"
    "CXXFLAGS=${_yb_rust_cxxflags}"
    # Tells the link-cplusplus crate (a cxx dependency) the standard library is libc++.
    CXXSTDLIB=c++
    "CARGO_TARGET_DIR=${YB_RUST_TARGET_DIR}"
    ASAN_OPTIONS=detect_leaks=0)

# Crates whose code only links inside a CMake-built executable (they call into the C++ host), so
# `cargo test` cannot link their test binaries.
set(YB_RUST_TEST_EXCLUDES --exclude master --exclude yb-cpp-host)

# Native libraries the Rust standard library needs (from `rustc --print native-static-libs`).
set(YB_RUST_NATIVE_LIBS gcc_s util rt pthread m dl)

# Runs Cargo on every build and lets it decide what is stale. The library is a BYPRODUCT, so with
# Ninja's restat a no-op Cargo run does not relink anything.
function(yb_add_rust_staticlib target_name package_name)
  string(REPLACE "-" "_" lib_basename "${package_name}")
  set(lib_path "${YB_RUST_TARGET_DIR}/${YB_RUST_PROFILE_DIR}/lib${lib_basename}.a")
  add_custom_target("${target_name}_cargo"
    COMMAND ${YB_RUST_CARGO_ENV}
            "${YB_CARGO_EXECUTABLE}" build --locked --profile "${YB_RUST_PROFILE}"
            -p "${package_name}"
    WORKING_DIRECTORY "${YB_RUST_SRC_DIR}"
    BYPRODUCTS "${lib_path}"
    COMMENT "Building Rust static library ${package_name}"
    USES_TERMINAL)
  add_library("${target_name}" STATIC IMPORTED GLOBAL)
  set_target_properties("${target_name}" PROPERTIES
    IMPORTED_LOCATION "${lib_path}"
    INTERFACE_LINK_LIBRARIES "${YB_RUST_NATIVE_LIBS}")
  add_dependencies("${target_name}" "${target_name}_cargo")
endfunction()

# Generates the C++ half of the #[cxx::bridge] modules in the given files (paths relative to
# src/rust) with the yb-cxxgen tool, which uses the same cxx version as the Rust side. Output for
# dir/src/x.rs is ${YB_RUST_CXXGEN_DIR}/dir/src/x.rs.{h,cc}; include the header as
# "dir/src/x.rs.h". Sets <out_var> to the generated .cc files. Exactly one call, in
# src/yb/rust_bridge, passes WITH_CXX_HEADER: the tool writes rust/cxx.h on every run, but only one
# rule may declare it as an output.
function(yb_rust_cxxgen out_var)
  cmake_parse_arguments(ARG "WITH_CXX_HEADER" "" "" ${ARGN})
  set(cxxgen_tool "${YB_BUILD_ROOT}/rust/tools/${YB_RUST_PROFILE_DIR}/yb-cxxgen")
  if(NOT TARGET yb_cxxgen_tool)
    add_custom_target(yb_cxxgen_tool
      COMMAND ${YB_RUST_CARGO_ENV} "CARGO_TARGET_DIR=${YB_BUILD_ROOT}/rust/tools"
              "${YB_CARGO_EXECUTABLE}" build --locked --profile "${YB_RUST_PROFILE}" -p yb-cxxgen
      WORKING_DIRECTORY "${YB_RUST_SRC_DIR}"
      BYPRODUCTS "${cxxgen_tool}"
      COMMENT "Building yb-cxxgen"
      USES_TERMINAL)
  endif()
  set(inputs)
  set(outputs)
  set(generated_ccs)
  foreach(rel_path IN LISTS ARG_UNPARSED_ARGUMENTS)
    list(APPEND inputs "${YB_RUST_SRC_DIR}/${rel_path}")
    list(APPEND outputs
         "${YB_RUST_CXXGEN_DIR}/${rel_path}.h" "${YB_RUST_CXXGEN_DIR}/${rel_path}.cc")
    list(APPEND generated_ccs "${YB_RUST_CXXGEN_DIR}/${rel_path}.cc")
  endforeach()
  if(ARG_WITH_CXX_HEADER)
    list(APPEND outputs "${YB_RUST_CXXGEN_DIR}/rust/cxx.h")
  endif()
  add_custom_command(
    OUTPUT ${outputs}
    COMMAND "${cxxgen_tool}" "${YB_RUST_SRC_DIR}" "${YB_RUST_CXXGEN_DIR}"
            ${ARG_UNPARSED_ARGUMENTS}
    DEPENDS ${inputs} yb_cxxgen_tool "${cxxgen_tool}"
    COMMENT "Generating C++ for Rust cxx bridges")
  set(${out_var} ${generated_ccs} PARENT_SCOPE)
endfunction()

# Builds a binary crate from the workspace and copies it into the bin directory, where tests and
# tools look for executables. Adds a target named after the crate.
function(yb_add_rust_tool package_name)
  set(built "${YB_BUILD_ROOT}/rust/tools/${YB_RUST_PROFILE_DIR}/${package_name}")
  set(installed "${EXECUTABLE_OUTPUT_PATH}/${package_name}")
  add_custom_target("${package_name}" ALL
    COMMAND ${YB_RUST_CARGO_ENV} "CARGO_TARGET_DIR=${YB_BUILD_ROOT}/rust/tools"
            "${YB_CARGO_EXECUTABLE}" build --locked --profile "${YB_RUST_PROFILE}"
            -p "${package_name}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${built}" "${installed}"
    WORKING_DIRECTORY "${YB_RUST_SRC_DIR}"
    BYPRODUCTS "${built}" "${installed}"
    COMMENT "Building Rust tool ${package_name}"
    USES_TERMINAL)
endfunction()
