#!/usr/bin/env bash

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

# Build OpenTelemetry C++ SDK with static libraries for YugabyteDB

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YB_SRC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Configuration
OTEL_VERSION="v1.24.0"
# Default to installing in thirdparty directory (no sudo required)
OTEL_INSTALL_PREFIX="${OTEL_INSTALL_PREFIX:-${YB_SRC_ROOT}/thirdparty/installed/opentelemetry}"
BUILD_DIR="/tmp/opentelemetry-cpp-build"
NUM_JOBS="${NUM_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

echo "================================================================"
echo "Building OpenTelemetry C++ SDK ${OTEL_VERSION}"
echo "================================================================"
echo "Install prefix: ${OTEL_INSTALL_PREFIX}"
echo "Build directory: ${BUILD_DIR}"
echo "Parallel jobs: ${NUM_JOBS}"
echo ""

# Check for required tools
for tool in git cmake make; do
  if ! command -v "$tool" &> /dev/null; then
    echo "ERROR: $tool is required but not installed"
    exit 1
  fi
done

# Clean up old build directory
if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing old build directory..."
  rm -rf "${BUILD_DIR}"
fi

# Clone OpenTelemetry C++ SDK
echo "Cloning OpenTelemetry C++ SDK..."
git clone --recurse-submodules --depth 1 --branch "${OTEL_VERSION}" \
  https://github.com/open-telemetry/opentelemetry-cpp.git "${BUILD_DIR}"

cd "${BUILD_DIR}"

# Create build directory
mkdir -p build
cd build

echo ""
echo "Configuring OpenTelemetry build..."
echo ""

# Configure with CMake
# - Static libraries only (BUILD_SHARED_LIBS=OFF)
# - OTLP HTTP exporter (avoids gRPC/protobuf conflicts with YugabyteDB)
# - No examples, tests, or benchmarks
# - Use libc++ to match YugabyteDB's ABI (required for Linux builds)

# On Linux, use clang with libc++ to match YugabyteDB's ABI
# macOS uses libc++ by default with Apple clang
CMAKE_EXTRA_FLAGS=""
if [[ "$(uname)" != "Darwin" ]]; then
  # Find clang in YB's LLVM installation
  CLANG_PATH=""
  CLANGXX_PATH=""
  
  # Check /opt/yb-build/llvm for YB's clang installation
  for llvm_dir in /opt/yb-build/llvm/yb-llvm-*/; do
    if [[ -x "${llvm_dir}bin/clang++" ]]; then
      CLANG_PATH="${llvm_dir}bin/clang"
      CLANGXX_PATH="${llvm_dir}bin/clang++"
      break
    fi
  done
  
  # Fallback to system clang if not found
  if [[ -z "${CLANGXX_PATH}" ]]; then
    if command -v clang++ &> /dev/null; then
      CLANG_PATH="clang"
      CLANGXX_PATH="clang++"
    fi
  fi
  
  if [[ -n "${CLANGXX_PATH}" ]]; then
    echo "Using clang: ${CLANGXX_PATH}"
    CMAKE_EXTRA_FLAGS="-DCMAKE_C_COMPILER=${CLANG_PATH} -DCMAKE_CXX_COMPILER=${CLANGXX_PATH} -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++"
  else
    echo "WARNING: clang++ not found, using default compiler (may cause ABI issues)"
  fi
fi

# shellcheck disable=SC2086
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${OTEL_INSTALL_PREFIX}" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_CXX_STANDARD=17 \
  -DBUILD_SHARED_LIBS=OFF \
  -DWITH_OTLP_GRPC=OFF \
  -DWITH_OTLP_HTTP=OFF \
  -DWITH_HTTP_CLIENT_CURL=OFF \
  -DWITH_PROMETHEUS=OFF \
  -DWITH_ZIPKIN=OFF \
  -DWITH_JAEGER=OFF \
  -DWITH_ELASTICSEARCH=OFF \
  -DWITH_EXAMPLES=OFF \
  -DWITH_LOGS_PREVIEW=OFF \
  -DWITH_METRICS_PREVIEW=OFF \
  -DBUILD_TESTING=OFF \
  -DWITH_BENCHMARK=OFF \
  ${CMAKE_EXTRA_FLAGS}

echo ""
echo "Building OpenTelemetry (this may take a few minutes)..."
echo ""

# Build
make -j"${NUM_JOBS}"

echo ""
echo "Installing OpenTelemetry to ${OTEL_INSTALL_PREFIX}..."
echo ""

# Create install prefix directory if it doesn't exist
mkdir -p "${OTEL_INSTALL_PREFIX}"

# Install (no sudo - use a writable prefix or set OTEL_INSTALL_PREFIX)
if [[ -w "${OTEL_INSTALL_PREFIX}" ]]; then
  make install
else
  echo "ERROR: Install directory ${OTEL_INSTALL_PREFIX} is not writable"
  echo "Either:"
  echo "  1. Set OTEL_INSTALL_PREFIX to a writable location, or"
  echo "  2. Create the directory with appropriate permissions first"
  exit 1
fi

# Verify installation
echo ""
echo "Verifying installation..."
if [[ -f "${OTEL_INSTALL_PREFIX}/include/opentelemetry/version.h" ]]; then
  echo "✓ Headers installed"
else
  echo "✗ Headers not found"
  exit 1
fi

if [[ -f "${OTEL_INSTALL_PREFIX}/lib/libopentelemetry_trace.a" ]]; then
  echo "✓ Static libraries installed"
else
  echo "✗ Static libraries not found"
  exit 1
fi

# List installed libraries
echo ""
echo "Installed static libraries:"
ls -lh "${OTEL_INSTALL_PREFIX}/lib/"*.a | awk '{print "  " $9 " (" $5 ")"}'

# Clean up build directory
echo ""
echo "Removing build directory ${BUILD_DIR}..."
rm -rf "${BUILD_DIR}"
echo "Build directory removed"

echo ""
echo "================================================================"
echo "OpenTelemetry C++ SDK installed successfully!"
echo "================================================================"
echo ""
echo "Installation directory: ${OTEL_INSTALL_PREFIX}"
echo ""
echo "Next steps:"
echo "  1. Rebuild YugabyteDB: ./yb_build.sh release"
echo "  2. CMake will automatically detect and use OpenTelemetry"
echo ""
echo "To uninstall:"
echo "  rm -rf ${OTEL_INSTALL_PREFIX}"
echo ""

