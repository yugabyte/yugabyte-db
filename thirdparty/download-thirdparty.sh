#!/usr/bin/env bash
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

. "${BASH_SOURCE%/*}/thirdparty-common.sh"

EXPECTED_CHECKSUM_FILE=$YB_THIRDPARTY_DIR/thirdparty_src_checksums.txt

# Maps file name to its expected SHA 256 checksum. These are loaded by load_expected_checksums.
declare -A expected_checksums_by_name

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} <options>
Arguments:
  -h, --help
    Print usage

  --download-only
    Don't expand downloaded archives or run autoreconf

  --dest-dir <dest_dir>
    Destination directory. This is useful for testing. This directory will be used instead of the
    thirdparty directory.
EOT
}

delete_if_wrong_patchlevel() {
  local DIR=$1
  local PATCHLEVEL=$2
  if [[ $DOWNLOAD_ONLY -eq 0 && -d $DIR && ! -f $DIR/patchlevel-$PATCHLEVEL ]]; then
    echo It appears that $DIR is missing the latest local patches.
    echo Removing it so we re-download it.
    rm -Rf "$DIR"
  fi
}

fetch_and_expand() {
  local FILENAME=$1
  if [ -z "$FILENAME" ]; then
    fatal "Error: Must specify file to fetch"
  fi

  set +u
  local expected_checksum=${expected_checksums_by_name[$FILENAME]}
  set -u
  if [[ -z ${expected_checksum:-} ]]; then
    fatal "No expected checksum provided in $EXPECTED_CHECKSUM_FILE for $FILENAME"
  fi

  mkdir -p "$TP_DOWNLOAD_DIR"
  pushd "$TP_DOWNLOAD_DIR"
  if [[ -f $FILENAME ]] && \
    run_sha256sum --check <( echo "$expected_checksum  $FILENAME" ); then
    log "No need to re-download $FILENAME: checksum already correct"
  else
    log "Fetching $FILENAME"
    curl --remote-name "${CLOUDFRONT_URL_PREFIX}/${FILENAME}"
  fi
  popd

  mkdir -p "$TP_SOURCE_DIR"
  pushd "$TP_SOURCE_DIR"
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    log "Extracting $FILENAME in $TP_SOURCE_DIR"
    if echo "$FILENAME" | egrep -q '\.zip$'; then
      # -o -- force overwriting existing files
      unzip -q -o "$TP_DOWNLOAD_DIR/$FILENAME"
    elif echo "$FILENAME" | egrep -q '(\.tar\.gz|\.tgz)$'; then
      tar xf "$TP_DOWNLOAD_DIR/$FILENAME"
    else
      fatal "Error: unknown file format: $FILENAME"
    fi
    echo
  fi
  popd
}

run_autoreconf() {
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    autoreconf "$@"
  fi
}

load_expected_checksums() {
  if [[ ! -f $EXPECTED_CHECKSUM_FILE ]]; then
    fatal "Expected checksum file not found at $EXPECTED_CHECKSUM_FILE"
  fi

  while read -r line || [[ -n "$line" ]]; do
    local checksum=${line%%  *}
    local archive_name=${line#*  }
    if [[ ! $checksum =~ ^[0-9a-f]{64}$ ]]; then
      fatal "Invalid checksum: '$checksum' for archive name: '$archive_name' in" \
            "$EXPECTED_CHECKSUM_FILE. Expected to be a SHA-256 sum (64 hex characters)."
    fi
    expected_checksums_by_name[$archive_name]=$checksum
  done <"$EXPECTED_CHECKSUM_FILE"
}

load_expected_checksums

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
  # We have to set TP_DIR based on DEST_DIR, because we import vars.sh that sets a lot of variables
  # based on TP_DIR.
  TP_DIR=$DEST_DIR
  if [[ ! -d "$DEST_DIR/patches" && $DOWNLOAD_ONLY -eq 0 ]]; then
    ln -s "$SCRIPT_DIR/patches" "$DEST_DIR/patches"
  fi
  # We should not have to use the custom destination directory from this point on.
  unset DEST_DIR
else
  TP_DIR=$SCRIPT_DIR
fi

cd "$TP_DIR"

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
GPERFTOOLS_PATCHLEVEL=3
delete_if_wrong_patchlevel "$GPERFTOOLS_DIR" "$GPERFTOOLS_PATCHLEVEL"
if [ ! -d "$GPERFTOOLS_DIR" ]; then
  fetch_and_expand gperftools-${GPERFTOOLS_VERSION}.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$GPERFTOOLS_DIR"
    patch -p1 < "$TP_DIR"/patches/gperftools-Change-default-TCMALLOC_TRANSFER_NUM_OBJ-to-40.patch
    patch -p1 < "$TP_DIR"/patches/gperftools-hook-mi_force_unlock-on-OSX-instead-of-pthread_atfork.patch
    patch -p1 < "$TP_DIR"/patches/gperftools-Fix-finding-default-zone-on-Mac-Sierra.patch
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

SNAPPY_PATCHLEVEL=1
delete_if_wrong_patchlevel "$SNAPPY_DIR" "$SNAPPY_PATCHLEVEL"
if [ ! -d "$SNAPPY_DIR" ]; then
  fetch_and_expand snappy-${SNAPPY_VERSION}.tar.gz
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$SNAPPY_DIR"
    patch -p1 < "$TP_DIR"/patches/snappy-define-guard-macro.patch
    touch patchlevel-"$SNAPPY_PATCHLEVEL"
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
  if [[ ! -d $TP_SOURCE_DIR/rapidjson ]]; then
    fatal "Directory $TP_SOURCE_DIR/rapidjson was not extracted correctly from the rapidjson" \
          "archive. Contents of the source directory: $( ls -l "$TP_SOURCE_DIR" )."
  fi
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    mv "$TP_SOURCE_DIR/rapidjson" "$RAPIDJSON_DIR"
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

LLVM_PATCHLEVEL=1
delete_if_wrong_patchlevel "$LLVM_SOURCE" "$LLVM_PATCHLEVEL"
if [ ! -d "$LLVM_SOURCE" ]; then
  fetch_and_expand llvm-${LLVM_VERSION}.src.tar.gz

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$LLVM_SOURCE"
    patch -p1 < $TP_DIR/patches/llvm-fix-amazon-linux.patch
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

if is_linux && [[ ! -d $NVML_DIR ]]; then
  fetch_and_expand "nvml-${NVML_VERSION}.tar.gz"
fi

echo "---------------"
if [[ $DOWNLOAD_ONLY -eq 1 ]]; then
  log "Sources of thirdparty dependencies downloaded successfully into $TP_DOWNLOAD_DIR"
else
  log "Sources of thirdparty dependencies downloaded and extracted into $TP_SOURCE_DIR"
fi
