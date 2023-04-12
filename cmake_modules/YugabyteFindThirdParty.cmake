# Copyright (c) Yugabyte, Inc.
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

# Finding various third-party dependencies, mostly using find_package.

#[[

We are setting FPHSA_NAME_MISMATCHED to TRUE above to avoid warnings like this. We get a lot of
these and they are probably not a problem. Also they might be happening more frequently on macOS due
to its case-insensitive file system.

  The package name passed to `find_package_handle_standard_args` (GLOG) does
  not match the name of the calling package (GLog).  This can lead to
  problems in calling code that expects `find_package` result variables
  (e.g., `_FOUND`) to follow a certain pattern.
Call Stack (most recent call first):
  cmake_modules/FindGLog.cmake:35 (find_package_handle_standard_args)
  cmake_modules/YugabyteFindThirdPartyPackages.cmake:26 (find_package)
  CMakeLists.txt:966 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

]]

macro(yb_find_third_party_installed_dir)
  if("${YB_COMPILER_TYPE}" STREQUAL "")
    message(FATAL_ERROR "YB_COMPILER_TYPE is not set")
  endif()
  if(NOT DEFINED YB_LTO_ENABLED)
    message(FATAL_ERROR "YB_LTO_ENABLED is not defined")
  endif()

  # We support a layout of third-party dependencies where there are build-type-specific
  # subdirectories inside "build" and "installed" directories, such as clang15-x86_64,
  # clang15-full-lto-x86_64, or clang15-linuxbrew-full-lto-x86_64. The logic below should match how
  # the subdir_items list is constructed in https://bit.ly/yb_thirdparty_fs_layout_py.

  set(YB_THIRDPARTY_INSTALLED_DIR_BASE "${YB_THIRDPARTY_DIR}/installed")
  set(YB_THIRDPARTY_SPECIFIC_BASENAME "${YB_COMPILER_TYPE}")
  if(USING_LINUXBREW)
    set(YB_THIRDPARTY_SPECIFIC_BASENAME "${YB_THIRDPARTY_SPECIFIC_BASENAME}-linuxbrew")
  endif()
  if(YB_LTO_ENABLED)
    set(YB_THIRDPARTY_SPECIFIC_BASENAME "${YB_THIRDPARTY_SPECIFIC_BASENAME}-${YB_LINKING_TYPE}")
  endif()
  set(YB_THIRDPARTY_SPECIFIC_BASENAME "${YB_THIRDPARTY_SPECIFIC_BASENAME}-$ENV{YB_TARGET_ARCH}")

  set(YB_THIRDPARTY_INSTALLED_DIR_SPECIFIC
      "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/${YB_THIRDPARTY_SPECIFIC_BASENAME}")
  if(EXISTS "${YB_THIRDPARTY_INSTALLED_DIR_SPECIFIC}")
    message("Using the per-compiler-type/architecture subdirectory of third-party installed dir: "
            "${YB_THIRDPARTY_INSTALLED_DIR_SPECIFIC}")
    set(YB_THIRDPARTY_INSTALLED_DIR "${YB_THIRDPARTY_INSTALLED_DIR_SPECIFIC}")
  else()
    message("Per-compiler-type/architecture third-party install directory not found: "
            "${YB_THIRDPARTY_INSTALLED_DIR_SPECIFIC}, using default third-party install directory.")
    set(YB_THIRDPARTY_INSTALLED_DIR "${YB_THIRDPARTY_INSTALLED_DIR_BASE}")
  endif()
  set(YB_THIRDPARTY_INSTALLED_DIR "${YB_THIRDPARTY_INSTALLED_DIR}" CACHE STRING
      "Directory where the third-party dependencies are installed" FORCE)
endmacro()

macro(yb_find_third_party_dependencies)
  set(FPHSA_NAME_MISMATCHED TRUE)

  ## OpenSSL

  find_package(OpenSSL REQUIRED)
  include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(openssl
    SHARED_LIB "${OPENSSL_CRYPTO_LIBRARY}"
    DEPS ${OPENSSL_LIB_DEPS})

  message("OPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}")
  message("OPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}")
  message("OPENSSL_FOUND=${OPENSSL_FOUND}")
  message("OPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}")
  message("OPENSSL_LIBRARIES=${OPENSSL_LIBRARIES}")
  message("OPENSSL_VERSION=${OPENSSL_VERSION}")

  if (NOT "${OPENSSL_CRYPTO_LIBRARY}" MATCHES "^${YB_THIRDPARTY_DIR}/.*")
    message(FATAL_ERROR "OPENSSL_CRYPTO_LIBRARY not in ${YB_THIRDPARTY_DIR}.")
  endif()
  if (NOT "${OPENSSL_SSL_LIBRARY}" MATCHES "^${YB_THIRDPARTY_DIR}/.*")
    message(FATAL_ERROR "OPENSSL_SSL_LIBRARY not in ${YB_THIRDPARTY_DIR}.")
  endif()

  ## GLog
  find_package(GLog REQUIRED)
  include_directories(SYSTEM ${GLOG_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(glog
    STATIC_LIB "${GLOG_STATIC_LIB}"
    SHARED_LIB "${GLOG_SHARED_LIB}")
  list(APPEND YB_BASE_LIBS glog)

  find_package(CDS REQUIRED)
  include_directories(SYSTEM ${CDS_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(cds
    STATIC_LIB "${CDS_STATIC_LIB}"
    SHARED_LIB "${CDS_SHARED_LIB}")

  if (NOT APPLE)
    ## libunwind (dependent of glog)
    ## Doesn't build on OSX.
    if(IS_CLANG AND "${COMPILER_VERSION}" VERSION_GREATER_EQUAL "10.0.0")
      set(UNWIND_HAS_ARCH_SPECIFIC_LIB FALSE)
    else()
      set(UNWIND_HAS_ARCH_SPECIFIC_LIB TRUE)
    endif()
    find_package(LibUnwind REQUIRED)
    include_directories(SYSTEM ${UNWIND_INCLUDE_DIR})
    ADD_THIRDPARTY_LIB(unwind
      STATIC_LIB "${UNWIND_STATIC_LIB}"
      SHARED_LIB "${UNWIND_SHARED_LIB}")
    if(UNWIND_HAS_ARCH_SPECIFIC_LIB)
      ADD_THIRDPARTY_LIB(unwind-arch
          STATIC_LIB "${UNWIND_STATIC_ARCH_LIB}"
          SHARED_LIB "${UNWIND_SHARED_ARCH_LIB}")
    endif()
    list(APPEND YB_BASE_LIBS unwind)

    ## libuuid -- also only needed on Linux as it is part of system libraries on macOS.
    find_package(LibUuid REQUIRED)
    include_directories(SYSTEM ${LIBUUID_INCLUDE_DIR})
    ADD_THIRDPARTY_LIB(libuuid
      STATIC_LIB "${LIBUUID_STATIC_LIB}"
      SHARED_LIB "${LIBUUID_SHARED_LIB}")
  endif()

  ## GFlags
  find_package(GFlags REQUIRED)
  include_directories(SYSTEM ${GFLAGS_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(gflags
    STATIC_LIB "${GFLAGS_STATIC_LIB}"
    SHARED_LIB "${GFLAGS_SHARED_LIB}")
  list(APPEND YB_BASE_LIBS gflags)

  ## GTest
  find_package(GTest REQUIRED)
  include_directories(SYSTEM ${GTEST_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(gmock
    STATIC_LIB ${GMOCK_STATIC_LIBRARY}
    SHARED_LIB ${GMOCK_SHARED_LIBRARY})
  ADD_THIRDPARTY_LIB(gtest
    STATIC_LIB ${GTEST_STATIC_LIBRARY}
    SHARED_LIB ${GTEST_SHARED_LIBRARY})

  ## Protobuf
  add_custom_target(gen_proto)
  find_package(Protobuf REQUIRED)
  include_directories(SYSTEM ${PROTOBUF_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(protobuf
    STATIC_LIB "${PROTOBUF_STATIC_LIBRARY}"
    SHARED_LIB "${PROTOBUF_SHARED_LIBRARY}")
  ADD_THIRDPARTY_LIB(protoc
    STATIC_LIB "${PROTOBUF_PROTOC_STATIC_LIBRARY}"
    SHARED_LIB "${PROTOBUF_PROTOC_SHARED_LIBRARY}"
    DEPS protobuf)
  find_package(YRPC REQUIRED)
  list(APPEND YB_BASE_LIBS protobuf)

  ## Snappy
  find_package(Snappy REQUIRED)
  include_directories(SYSTEM ${SNAPPY_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(snappy
    STATIC_LIB "${SNAPPY_STATIC_LIB}"
    SHARED_LIB "${SNAPPY_SHARED_LIB}")

  ## Libev
  find_package(LibEv REQUIRED)
  include_directories(SYSTEM ${LIBEV_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(libev
    STATIC_LIB "${LIBEV_STATIC_LIB}"
    SHARED_LIB "${LIBEV_SHARED_LIB}")

  ## libbacktrace
  if(NOT APPLE)
  find_package(LibBacktrace REQUIRED)
  include_directories(SYSTEM ${LIBBACKTRACE_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(libbacktrace
    STATIC_LIB "${LIBBACKTRACE_STATIC_LIB}"
    SHARED_LIB "${LIBBACKTRACE_SHARED_LIB}")
  endif()

  ## LZ4
  find_package(Lz4 REQUIRED)
  include_directories(SYSTEM ${LZ4_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(lz4 STATIC_LIB "${LZ4_STATIC_LIB}")

  ## ZLib
  find_package(Zlib REQUIRED)
  include_directories(SYSTEM ${ZLIB_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(zlib
    STATIC_LIB "${ZLIB_STATIC_LIB}"
    SHARED_LIB "${ZLIB_SHARED_LIB}")

  ## Squeasel
  find_package(Squeasel REQUIRED)
  include_directories(SYSTEM ${SQUEASEL_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(squeasel
    STATIC_LIB "${SQUEASEL_STATIC_LIB}")

  ## Hiredis
  find_package(Hiredis REQUIRED)
  include_directories(SYSTEM ${HIREDIS_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(hiredis STATIC_LIB "${HIREDIS_STATIC_LIB}")

  # -------------------------------------------------------------------------------------------------
  # Deciding whether to use tcmalloc
  # -------------------------------------------------------------------------------------------------

  # Do not use tcmalloc for ASAN/TSAN.
  if ("${YB_TCMALLOC_ENABLED}" STREQUAL "")
    if ("${YB_BUILD_TYPE}" MATCHES "^(asan|tsan)$")
      set(YB_TCMALLOC_ENABLED "0")
      message("Not using tcmalloc due to build type ${YB_BUILD_TYPE}")
    else()
      set(YB_TCMALLOC_ENABLED "1")
      message("Using tcmalloc by default, build type is ${YB_BUILD_TYPE}, "
              "architecture is ${CMAKE_SYSTEM_PROCESSOR}.")
    endif()
  else()
    if (NOT "${YB_TCMALLOC_ENABLED}" MATCHES "^[01]$")
      message(FATAL_ERROR
              "YB_TCMALLOC_ENABLED has an invalid value '${YB_TCMALLOC_ENABLED}'. Can be 0, 1, or "
              "empty (undefined).")
    endif()

    message("YB_TCMALLOC_ENABLED is already set to '${YB_TCMALLOC_ENABLED}'")
  endif()

  if ("${YB_TCMALLOC_ENABLED}" STREQUAL "1")
    if ("${YB_GOOGLE_TCMALLOC}" STREQUAL "1")
      message("Using google/tcmalloc")
      find_package(TCMalloc REQUIRED)

      # Use abseil (which tcmalloc depends on).
      find_package(Abseil REQUIRED)
      ADD_THIRDPARTY_LIB(abseil
        STATIC_LIB "${ABSEIL_STATIC_LIB}"
        SHARED_LIB "${ABSEIL_SHARED_LIB}")
      ADD_CXX_FLAGS("-DYB_GOOGLE_TCMALLOC")
    else()
      ## Google PerfTools
      ##
      message("Using gperftools/tcmalloc")
      find_package(GPerf REQUIRED)

      # libprofiler can be linked dynamically into non-LTO executables.
      ADD_THIRDPARTY_LIB(profiler
        STATIC_LIB "${PROFILER_STATIC_LIB}"
        SHARED_LIB "${PROFILER_SHARED_LIB}")
      ADD_CXX_FLAGS("-DYB_GPERFTOOLS_TCMALLOC")
    endif()

    # We link tcmalloc statically into every executable, so we are not interested in the shared
    # tcmalloc library here.
    ADD_THIRDPARTY_LIB(tcmalloc STATIC_LIB "${TCMALLOC_STATIC_LIB}")
    ADD_CXX_FLAGS("-DYB_TCMALLOC_ENABLED")
  else()
    message("Not using tcmalloc, YB_TCMALLOC_ENABLED is '${YB_TCMALLOC_ENABLED}'")
  endif()

  #
  # -------------------------------------------------------------------------------------------------

  ## curl
  find_package(CURL REQUIRED)
  include_directories(SYSTEM ${CURL_INCLUDE_DIRS})
  ADD_THIRDPARTY_LIB(curl STATIC_LIB "${CURL_LIBRARIES}")

  ## crcutil
  find_package(Crcutil REQUIRED)
  include_directories(SYSTEM ${CRCUTIL_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(crcutil
    STATIC_LIB "${CRCUTIL_STATIC_LIB}"
    SHARED_LIB "${CRCUTIL_SHARED_LIB}")

  ## crypt_blowfish -- has no shared library!
  find_package(CryptBlowfish REQUIRED)
  include_directories(SYSTEM ${CRYPT_BLOWFISH_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(crypt_blowfish
    STATIC_LIB "${CRYPT_BLOWFISH_STATIC_LIB}")

  ## librt
  if (NOT APPLE)
    find_library(RT_LIB_PATH rt)
    if(RT_LIB_PATH)
      ADD_THIRDPARTY_LIB(rt SHARED_LIB "${RT_LIB_PATH}")
    else()
      message(WARNING "Could not find librt on the system path, proceeding without it.")
    endif()
  endif()

  ## Boost

  # BOOST_ROOT is used by find_package(Boost ...) below.
  set(BOOST_ROOT "${YB_THIRDPARTY_INSTALLED_DEPS_DIR}")

  # Workaround for
  # http://stackoverflow.com/questions/9948375/cmake-find-package-succeeds-but-returns-wrong-path
  set(Boost_NO_BOOST_CMAKE ON)
  set(Boost_NO_SYSTEM_PATHS ON)

  if("${YB_COMPILER_TYPE}" MATCHES "^gcc[0-9]+$")
    # TODO: display this only if using a devtoolset compiler on CentOS, and ideally only if the error
    # actually happens.
    message("Note: if Boost fails to find Threads, you might need to install the "
            "devtoolset-N-libatomic-devel package for the devtoolset you are using.")
  endif()

  # Find Boost static libraries.
  set(Boost_USE_STATIC_LIBS ON)
  find_package(Boost COMPONENTS system thread atomic REQUIRED)
  show_found_boost_details("static")

  set(BOOST_STATIC_LIBS ${Boost_LIBRARIES})
  list(LENGTH BOOST_STATIC_LIBS BOOST_STATIC_LIBS_LEN)
  list(SORT BOOST_STATIC_LIBS)

  # Find Boost shared libraries.
  set(Boost_USE_STATIC_LIBS OFF)
  find_package(Boost COMPONENTS system thread atomic REQUIRED)
  show_found_boost_details("shared")
  set(BOOST_SHARED_LIBS ${Boost_LIBRARIES})
  list(LENGTH BOOST_SHARED_LIBS BOOST_SHARED_LIBS_LEN)
  list(SORT BOOST_SHARED_LIBS)

  # We should have found the same number of libraries both times.
  if(NOT ${BOOST_SHARED_LIBS_LEN} EQUAL ${BOOST_STATIC_LIBS_LEN})
    set(ERROR_MSG "Boost static and shared libraries are inconsistent.")
    set(ERROR_MSG "${ERROR_MSG} Static libraries: ${BOOST_STATIC_LIBS}.")
    set(ERROR_MSG "${ERROR_MSG} Shared libraries: ${BOOST_SHARED_LIBS}.")
    message(FATAL_ERROR "${ERROR_MSG}")
  endif()

  # Add each pair of static/shared libraries.
  math(EXPR LAST_IDX "${BOOST_STATIC_LIBS_LEN} - 1")
  foreach(IDX RANGE ${LAST_IDX})
    list(GET BOOST_STATIC_LIBS ${IDX} BOOST_STATIC_LIB)
    list(GET BOOST_SHARED_LIBS ${IDX} BOOST_SHARED_LIB)

    # Remove the prefix/suffix from the library name.
    #
    # e.g. libboost_system-mt --> boost_system
    get_filename_component(LIB_NAME ${BOOST_STATIC_LIB} NAME_WE)
    string(REGEX REPLACE "lib([^-]*)(-mt)?" "\\1" LIB_NAME_NO_PREFIX_SUFFIX ${LIB_NAME})
    ADD_THIRDPARTY_LIB(${LIB_NAME_NO_PREFIX_SUFFIX}
      # Boost's static library is not compiled with PIC so it cannot be used to link a shared
      # library. So skip that and use the shared library instead.
      # STATIC_LIB "${BOOST_STATIC_LIB}"
      SHARED_LIB "${BOOST_SHARED_LIB}")
    list(APPEND YB_BASE_LIBS ${LIB_NAME_NO_PREFIX_SUFFIX})
  endforeach()
  include_directories(SYSTEM ${Boost_INCLUDE_DIR})

  find_package(ICU COMPONENTS i18n uc REQUIRED)
  include_directories(SYSTEM ${ICU_INCLUDE_DIRS})
  ADD_THIRDPARTY_LIB(icui18n
    STATIC_LIB "${ICU_I18N_LIBRARIES}"
    SHARED_LIB "${ICU_I18N_LIBRARIES}")
  ADD_THIRDPARTY_LIB(icuuc
    STATIC_LIB "${ICU_UC_LIBRARIES}"
    SHARED_LIB "${ICU_UC_LIBRARIES}")
  message("ICU_I18N_LIBRARIES=${ICU_I18N_LIBRARIES}")
  message("ICU_UC_LIBRARIES=${ICU_UC_LIBRARIES}")

  ## Libuv
  find_package(LibUv REQUIRED)
  include_directories(SYSTEM ${LIBUV_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(libuv
    STATIC_LIB "${LIBUV_STATIC_LIB}"
    SHARED_LIB "${LIBUV_SHARED_LIB}")

  ## C++ Cassandra driver
  find_package(Cassandra REQUIRED)
  include_directories(SYSTEM ${LIBCASSANDRA_INCLUDE_DIR})
  ADD_THIRDPARTY_LIB(cassandra SHARED_LIB "${LIBCASSANDRA_SHARED_LIB}")

  ## Otel
  set(OTEL_INCLUDE_DIR "${YB_THIRDPARTY_INSTALLED_DIR}/include")
  set(OTEL_TRACE_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_trace.so")
  set(OTEL_EXPORTER_MEMORY_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_exporter_in_memory.so")
  set(OTEL_EXPORTER_OSTREAM_METRICS_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_exporter_ostream_metrics.so")
  set(OTEL_EXPORTER_OSTREAM_SPAN_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_exporter_ostream_span.so")
  set(OTEL_EXPORTER_OTLP_HTTP_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_exporter_otlp_http.so")
  set(OTEL_OTLP_RECORDABLE_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_otlp_recordable.so")
  set(OTEL_COMMON_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_common.so")
  set(OTEL_METRICS_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_metrics.so")
  set(OTEL_RESOURCES_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_resources.so")
  set(OTEL_VERSION_SHARED_LIB "${YB_THIRDPARTY_INSTALLED_DIR_BASE}/uninstrumented/lib/libopentelemetry_version.so")

  find_package(opentelemetry-cpp CONFIG REQUIRED)
  include_directories(SYSTEM ${OTEL_INCLUDE_DIR})

  ADD_THIRDPARTY_LIB(opentelemetry_trace SHARED_LIB "${OTEL_TRACE_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_exporter_in_memory SHARED_LIB "${OTEL_EXPORTER_MEMORY_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_exporter_ostream_metrics SHARED_LIB "${OTEL_EXPORTER_OSTREAM_METRICS_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_exporter_otlp_http SHARED_LIB "${OTEL_EXPORTER_OTLP_HTTP_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_otlp_recordable SHARED_LIB "${OTEL_OTLP_RECORDABLE_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_exporter_ostream_span SHARED_LIB "${OTEL_EXPORTER_OSTREAM_SPAN_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_common SHARED_LIB "${OTEL_COMMON_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_metrics SHARED_LIB "${OTEL_METRICS_SHARED_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_resources SHARED_LIB "${OTEL_RESOURCES_LIB}")
  ADD_THIRDPARTY_LIB(opentelemetry_version SHARED_LIB "${OTEL_VERSION_SHARED_LIB}")

  list(APPEND YB_BASE_LIBS
    opentelemetry_trace
    opentelemetry_exporter_in_memory
    opentelemetry_exporter_ostream_metrics
    opentelemetry_exporter_otlp_http
    opentelemetry_otlp_recordable
    opentelemetry_exporter_ostream_span
    opentelemetry_common
    opentelemetry_metrics
    opentelemetry_resources
    opentelemetry_version)

endmacro()
