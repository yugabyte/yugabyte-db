#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# autoreconf calls are necessary to fix hard-coded aclocal versions in the
# configure scripts that ship with the projects.

set -euo pipefail

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} <options>
Arguments:
  -h, --help
    Print usage
  --download-only
    Don't expand downloaded archives or run autoreconf
  --dest-dir <dest_dir>
    Destination directory.
EOT
}

delete_if_wrong_patchlevel() {
  local DIR=$1
  local PATCHLEVEL=$2
  if [ ! -f $DIR/patchlevel-$PATCHLEVEL ]; then
    echo It appears that $DIR is missing the latest local patches.
    echo Removing it so we re-download it.
    rm -Rf $DIR
  fi
}

fetch_and_expand() {
  local FILENAME=$1
  if [ -z "$FILENAME" ]; then
    echo "Error: Must specify file to fetch"
    exit 1
  fi

  echo "Fetching $FILENAME"
  curl -O "${CLOUDFRONT_URL_PREFIX}/${FILENAME}"

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    echo "Unpacking $FILENAME"
    if echo "$FILENAME" | egrep -q '\.zip$'; then
      unzip -q "$FILENAME"
    elif echo "$FILENAME" | egrep -q '(\.tar\.gz|\.tgz)$'; then
      tar xf "$FILENAME"
    else
      echo "Error: unknown file format: $FILENAME"
      exit 1
    fi

    echo "Removing $FILENAME"
    rm "$FILENAME"
    echo
  fi
}

run_autoreconf() {
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    autoreconf "$@"
  fi
}

DOWNLOAD_ONLY=0
DEST_DIR=""
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --download-only)
      DOWNLOAD_ONLY=1
    ;;
    --dest-dir)
      DEST_DIR=$2
      shift
    ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" && pwd)
if [ -n "$DEST_DIR" ]; then
  echo "Using custom download directory '$DEST_DIR'"
  mkdir -p "$DEST_DIR"
  TP_DIR=$DEST_DIR
  if [ ! -d "$DEST_DIR/patches" ]; then
    ln -s "$SCRIPT_DIR/patches" "$DEST_DIR/patches"
  fi
else
  TP_DIR=$SCRIPT_DIR
fi

cd "$TP_DIR"

if [[ "$OSTYPE" =~ ^linux ]]; then
  OS_LINUX=1
fi

source "$SCRIPT_DIR/vars.sh"


GLOG_PATCHLEVEL=1
delete_if_wrong_patchlevel "$GLOG_DIR" "$GLOG_PATCHLEVEL"
if [ ! -d "$GLOG_DIR" ]; then
  fetch_and_expand glog-${GLOG_VERSION}.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$GLOG_DIR"
    patch -p0 < "$TP_DIR"/patches/glog-issue-198-fix-unused-warnings.patch
    touch patchlevel-"$GLOG_PATCHLEVEL"
    run_autoreconf -fvi
    popd
  fi
  echo
fi

if [ ! -d "$GMOCK_DIR" ]; then
  fetch_and_expand gmock-${GMOCK_VERSION}.zip
fi

if [ ! -d "$GFLAGS_DIR" ]; then
  fetch_and_expand gflags-${GFLAGS_VERSION}.tar.gz
fi

# Check that the gperftools patch has been applied.
# If you add or remove patches, bump the patchlevel below to ensure
# that any new Jenkins builds pick up your patches.
GPERFTOOLS_PATCHLEVEL=2
delete_if_wrong_patchlevel "$GPERFTOOLS_DIR" "$GPERFTOOLS_PATCHLEVEL"
if [ ! -d "$GPERFTOOLS_DIR" ]; then
  fetch_and_expand gperftools-${GPERFTOOLS_VERSION}.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$GPERFTOOLS_DIR"
    patch -p1 < "$TP_DIR"/patches/gperftools-Change-default-TCMALLOC_TRANSFER_NUM_OBJ-to-40.patch
    patch -p1 < "$TP_DIR"/patches/gperftools-hook-mi_force_unlock-on-OSX-instead-of-pthread_atfork.patch
    touch patchlevel-"$GPERFTOOLS_PATCHLEVEL"
    run_autoreconf -fvi
    popd
  fi
  echo
fi

if [ ! -d "$PROTOBUF_DIR" ]; then
  fetch_and_expand protobuf-${PROTOBUF_VERSION}.tar.gz
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$PROTOBUF_DIR"
    run_autoreconf -fvi
    popd
  fi
fi

if [ ! -d "$CMAKE_DIR" ]; then
  fetch_and_expand cmake-${CMAKE_VERSION}.tar.gz
fi

if [ ! -d "$SNAPPY_DIR" ]; then
  fetch_and_expand snappy-${SNAPPY_VERSION}.tar.gz
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$SNAPPY_DIR"
    run_autoreconf -fvi
    popd
  fi
fi

if [ ! -d "$ZLIB_DIR" ]; then
  fetch_and_expand zlib-${ZLIB_VERSION}.tar.gz
fi

if [ ! -d "$LIBEV_DIR" ]; then
  fetch_and_expand libev-${LIBEV_VERSION}.tar.gz
fi

if [ ! -d "$RAPIDJSON_DIR" ]; then
  fetch_and_expand rapidjson-${RAPIDJSON_VERSION}.zip
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    mv rapidjson ${RAPIDJSON_DIR}
  fi
fi

if [ ! -d "$SQUEASEL_DIR" ]; then
  fetch_and_expand squeasel-${SQUEASEL_VERSION}.tar.gz
fi

if [ ! -d "$GSG_DIR" ]; then
  fetch_and_expand google-styleguide-${GSG_VERSION}.tar.gz
fi

if [ ! -d "$GCOVR_DIR" ]; then
  fetch_and_expand gcovr-${GCOVR_VERSION}.tar.gz
fi

if [ ! -d "$CURL_DIR" ]; then
  fetch_and_expand curl-${CURL_VERSION}.tar.gz
fi

CRCUTIL_PATCHLEVEL=1
delete_if_wrong_patchlevel "$CRCUTIL_DIR" "$CRCUTIL_PATCHLEVEL"
if [ ! -d "$CRCUTIL_DIR" ]; then
  fetch_and_expand crcutil-${CRCUTIL_VERSION}.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$CRCUTIL_DIR"
    patch -p0 < "$TP_DIR/patches/crcutil-fix-libtoolize-on-osx.patch"
    touch "patchlevel-$CRCUTIL_PATCHLEVEL"
    popd
  fi
  echo
fi

if [ ! -d "$LIBUNWIND_DIR" ]; then
  fetch_and_expand libunwind-${LIBUNWIND_VERSION}.tar.gz
fi

if [ ! -d "$PYTHON_DIR" ]; then
  fetch_and_expand python-${PYTHON_VERSION}.tar.gz
fi

LLVM_PATCHLEVEL=2
delete_if_wrong_patchlevel "$LLVM_DIR" "$LLVM_PATCHLEVEL"
if [ ! -d "$LLVM_DIR" ]; then
  fetch_and_expand llvm-${LLVM_VERSION}.src.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$LLVM_DIR"
    patch -p1 < $TP_DIR/patches/llvm-fix-amazon-linux.patch
    patch -p1 < $TP_DIR/patches/llvm-devtoolset-toolchain.patch
    touch patchlevel-$LLVM_PATCHLEVEL
    popd
  fi
  echo
fi

GCC_PATCHLEVEL=2
delete_if_wrong_patchlevel $GCC_DIR $GCC_PATCHLEVEL
if [[ "$OSTYPE" =~ ^linux ]] && [[ ! -d "$GCC_DIR" ]]; then
  fetch_and_expand gcc-${GCC_VERSION}.tar.gz
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd $GCC_DIR/libstdc++-v3
    patch -p0 < $TP_DIR/patches/libstdcxx-fix-string-dtor.patch
    patch -p0 < $TP_DIR/patches/libstdcxx-fix-tr1-shared-ptr.patch
    cd ..
    touch patchlevel-$GCC_PATCHLEVEL
    popd
  fi
  echo
fi

LZ4_PATCHLEVEL=1
delete_if_wrong_patchlevel "$LZ4_DIR" "$LZ4_PATCHLEVEL"
if [ ! -d "$LZ4_DIR" ]; then
  fetch_and_expand "lz4-lz4-$LZ4_VERSION.tar.gz"
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$LZ4_DIR"
    patch -p1 < $TP_DIR/patches/lz4-0001-Fix-cmake-build-to-use-gnu-flags-on-clang.patch
    touch patchlevel-$LZ4_PATCHLEVEL
    popd
  fi
  echo
fi

if [ ! -d "$BITSHUFFLE_DIR" ]; then
  fetch_and_expand bitshuffle-${BITSHUFFLE_VERSION}.tar.gz
fi

if [ ! -d "$TRACE_VIEWER_DIR" ]; then
  fetch_and_expand "kudu-trace-viewer-${TRACE_VIEWER_VERSION}.tar.gz"
fi

if [ -n "$OS_LINUX" -a ! -d $NVML_DIR ]; then
  fetch_and_expand "nvml-${NVML_VERSION}.tar.gz"
fi

echo "---------------"
echo "Thirdparty dependencies downloaded successfully"
