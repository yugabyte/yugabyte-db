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
set(POSTGRES_EXTRA_LD_FLAGS "")

get_property(yb_cmake_include_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTY INCLUDE_DIRECTORIES)

macro(add_pg_c_cxx_flags)
  foreach(_tmp_pg_flag IN ITEMS ${ARGN})
    if(NOT POSTGRES_EXTRA_C_CXX_FLAGS MATCHES "[ ]$")
      string(APPEND POSTGRES_EXTRA_C_CXX_FLAGS " ")
    endif()
    string(APPEND POSTGRES_EXTRA_C_CXX_FLAGS "${_tmp_pg_flag}")
  endforeach()
endmacro()

macro(add_pg_ld_flags)
  foreach(_tmp_pg_flag IN ITEMS ${ARGN})
    if(NOT POSTGRES_EXTRA_LD_FLAGS MATCHES "[ ]$")
      string(APPEND POSTGRES_EXTRA_LD_FLAGS " ")
    endif()
    string(APPEND POSTGRES_EXTRA_LD_FLAGS "${_tmp_pg_flag}")
  endforeach()
endmacro()

set(added_pg_include_directories "")

macro(add_pg_include_directory include_dir)
  set(_tmp_duplicate_include_dir OFF)
  foreach(_tmp_existing_include_dir IN LISTS added_pg_include_directories)
    if("${_tmp_existing_include_dir}" STREQUAL "${include_dir}")
      set(_tmp_duplicate_include_dir ON)
    endif()
  endforeach()
  list(APPEND added_pg_include_directories "${include_dir}")
  if(NOT _tmp_duplicate_include_dir)
    add_pg_c_cxx_flags("-I${include_dir}")
  endif()
endmacro()

foreach(include_dir ${yb_cmake_include_dirs})
  add_pg_include_directory("${include_dir}")
endforeach(include_dir)

if(IS_GCC)
  add_pg_c_cxx_flags(
    -Wno-format-truncation
    -Wno-maybe-uninitialized
    -Wno-stringop-truncation
  )
endif()

if(IS_CLANG)
  if("${COMPILER_VERSION}" VERSION_GREATER_EQUAL "13.0.0" AND NOT IS_APPLE_CLANG)
    add_pg_c_cxx_flags(-Wno-error=unused-but-set-variable)
  endif()

  if("${COMPILER_VERSION}" VERSION_GREATER_EQUAL "15.0.0")
    add_pg_c_cxx_flags(-Wno-error=deprecated-non-prototype)
  endif()
endif()

if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
  add_pg_c_cxx_flags(-DUSE_SSE42_CRC32C=1)
endif()

if(NOT APPLE AND
   "${YB_BUILD_TYPE}" STREQUAL "asan" AND
   IS_CLANG AND
   "${COMPILER_VERSION}" VERSION_GREATER_EQUAL "10.0.0")
  add_pg_ld_flags(-ldl)
endif()

get_filename_component(OPENSSL_LIB_DIR "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
message("OPENSSL_LIB_DIR=${OPENSSL_LIB_DIR}")

set(POSTGRES_PKG_CONFIG_PATH "${YB_THIRDPARTY_INSTALLED_DEPS_DIR}/lib/pkgconfig")

set(build_postgres_args
    "${CMAKE_COMMAND}"
    -E
    env "YB_BUILD_ROOT=$ENV{YB_BUILD_ROOT}"
    env "PKG_CONFIG_PATH=${POSTGRES_PKG_CONFIG_PATH}")

if (APPLE AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
  list(APPEND build_postgres_args arch -arm64)
endif()

set(POSTGRES_EXTRA_PREPROCESSOR_FLAGS "")
if(APPLE AND NOT "${MACOS_SDK_INCLUDE_DIR}" STREQUAL "")
  string(APPEND POSTGRES_EXTRA_PREPROCESSOR_FLAGS
         " -isystem ${MACOS_SDK_INCLUDE_DIR}")
endif()

set(PG_INCLUDE_DIRS "${YB_PREFIX_COMMON}/include")
set(PG_LIB_DIRS "${YB_PREFIX_COMMON}/lib")
if(APPLE AND NOT IS_APPLE_CLANG)
  add_pg_ld_flags("-L${CMAKE_OSX_SYSROOT}/usr/lib")
  add_pg_c_cxx_flags("-isysroot ${CMAKE_OSX_SYSROOT}")
  # This is needed for PostgreSQL's configure to find headers such as uuid.h.
  string(APPEND PG_INCLUDE_DIRS " ${CMAKE_OSX_SYSROOT}/usr/include")
endif()
set(POSTGRES_FINAL_C_FLAGS "${POSTGRES_EXTRA_C_CXX_FLAGS} ${CMAKE_C_FLAGS}")
yb_deduplicate_arguments(POSTGRES_FINAL_C_FLAGS)

set(POSTGRES_FINAL_CXX_FLAGS "${POSTGRES_EXTRA_C_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")
yb_deduplicate_arguments(POSTGRES_FINAL_CXX_FLAGS)

set(POSTGRES_FINAL_LD_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${POSTGRES_EXTRA_LD_FLAGS}")
yb_deduplicate_arguments(POSTGRES_FINAL_LD_FLAGS)

set(POSTGRES_FINAL_EXE_LD_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${POSTGRES_EXTRA_LD_FLAGS}")
yb_deduplicate_arguments(POSTGRES_FINAL_EXE_LD_FLAGS)

set(PG_EXE_LD_FLAGS_AFTER_YB_LIBS "")
if("${YB_TCMALLOC_ENABLED}" STREQUAL "1")
  # We don't add libtcmalloc and libprofiler to POSTGRES_FINAL_EXE_LD_FLAGS because when building
  # with GCC we use the standard ld linker, which cares about the order of libraries when using
  # static libraries. Symbols from libtcmalloc and libprofiler are needed by e.g. libyb_util, so
  # libtcmalloc must appear after other YB libraries on the linker command line.
  #
  # The special variable PG_EXE_LD_FLAGS_AFTER_YB_LIBS eventually gets propagated to a
  # YB_PG_EXE_LD_FLAGS_AFTER_YB_LIBS environment variable used in Postgres makefiles that build
  # an executable. It gets included after all C++ YB libs that the relevant executable depends on.
  if ("${YB_GOOGLE_TCMALLOC}" STREQUAL "1")
    set(PG_EXE_LD_FLAGS_AFTER_YB_LIBS "${TCMALLOC_STATIC_LIB_LD_FLAGS} -labsl")
  else()
    set(PG_EXE_LD_FLAGS_AFTER_YB_LIBS "${TCMALLOC_STATIC_LIB_LD_FLAGS} -lprofiler")
  endif()

  if(IS_CLANG)
    string(APPEND PG_EXE_LD_FLAGS_AFTER_YB_LIBS " -lc++")
  endif()
  if(IS_GCC)
    # To avoid these errors when linking postgres executables:
    # https://gist.githubusercontent.com/mbautin/c6f0e741de57e671e78b78e9a8508995/raw
    # https://gist.githubusercontent.com/mbautin/6cbef12e6bbab79d1f87964f571d2d46/raw
    string(APPEND PG_EXE_LD_FLAGS_AFTER_YB_LIBS " -lstdc++ -lunwind")
  endif()
endif()

yb_put_vars_into_cache(
  POSTGRES_FINAL_C_FLAGS
  POSTGRES_FINAL_CXX_FLAGS
  POSTGRES_FINAL_LD_FLAGS
  POSTGRES_FINAL_EXE_LD_FLAGS
  POSTGRES_EXTRA_PREPROCESSOR_FLAGS
  PG_EXE_LD_FLAGS_AFTER_YB_LIBS
  PG_INCLUDE_DIRS
  PG_LIB_DIRS
)

list(APPEND build_postgres_args
     "${BUILD_SUPPORT_DIR}/build_postgres" --build_root "${YB_BUILD_ROOT}")

if (NOT DEFINED LIBPQ_SHARED_LIB)
  message(FATAL_ERROR "LIBPQ_SHARED_LIB must be defined")
endif()
if (NOT DEFINED YB_PGBACKEND_SHARED_LIB)
  message(FATAL_ERROR "YB_PGBACKEND_SHARED_LIB must be defined")
endif()

# Invoke our PostgreSQL build script in two steps: "configure" and "make". The "configure" step
# does not have any dependencies so it can run in parallel with e.g. C++ code compilation.
#
# We add spaces before every "flags" parameter so that Python's argparse does not interpret them as
# its own flags.
add_custom_target(configure_postgres ALL COMMAND ${build_postgres_args} --step configure)
add_custom_target(
    postgres ALL
    COMMAND ${build_postgres_args} --step make
    BYPRODUCTS "${LIBPQ_SHARED_LIB}" "${YB_PGBACKEND_SHARED_LIB}")

add_dependencies(postgres configure_postgres)

# ------------------------------------------------------------------------------------------------
# Dependencies between postgres and other targets
# ------------------------------------------------------------------------------------------------

# Libraries that we link into the postgres binary.
add_dependencies(postgres yb_pggate server_process)
