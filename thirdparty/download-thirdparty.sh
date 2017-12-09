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

verify_archive_checksum() {
  expect_num_args 2 "$@"
  local file_name=$1
  local expected_checksum=$2
  verify_sha256sum <( echo "$expected_checksum  $file_name" )
}

ensure_file_downloaded() {
  expect_num_args 2 "$@"
  local download_url=$1
  local filename=$2

  set +u
  local expected_checksum=${expected_checksums_by_name[$filename]}
  set -u
  if [[ -z ${expected_checksum:-} ]]; then
    fatal "No expected checksum provided in $EXPECTED_CHECKSUM_FILE for $filename"
  fi

  mkdir -p "$TP_DOWNLOAD_DIR"
  pushd "$TP_DOWNLOAD_DIR"
  if [[ -f $filename ]] && verify_archive_checksum "$filename" "$expected_checksum"; then
    log "No need to re-download $filename: checksum already correct"
  else
    if [[ -f $filename ]]; then
      log "File $PWD/$filename already exists but has wrong checksum, removing"
      ( set -x; rm -f "$filename" )
    fi
    log "Fetching $filename"
    if [[ $download_url == s3:* ]]; then
      s3cmd get "$download_url" "$filename"
      # Alternatively we can use AWS CLI:
      # aws s3 cp "$download_url" "$FILENAME"
    else
      curl "$download_url" --location --output "$filename"
    fi
    if [[ ! -f $filename ]]; then
      fatal "Downloaded '$download_url' but remote header name did not match '$filename'.."
    fi
    if ! verify_archive_checksum "$filename" "$expected_checksum"; then
      fatal "File '$PWD/$filename' has wrong checksum after downloading from '$download_url'."
    fi
  fi
  popd
}

extract_archive() {
  expect_num_args 2 "$@"
  local filename=$1
  local dir=$2

  mkdir -p "$dir"
  pushd "$dir"
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    log "Extracting $filename in $dir"
    if echo "$filename" | egrep -q '\.zip$'; then
      # -o -- force overwriting existing files
      unzip -q -o "$TP_DOWNLOAD_DIR/$filename"
    elif echo "$filename" | egrep -q '(\.tar\.gz|\.tgz|\.tar\.xz)$'; then
      tar xf "$TP_DOWNLOAD_DIR/$filename"
    else
      fatal "Error: unknown file format: $filename"
    fi
    echo
  fi
  popd
}

fetch_and_expand() {
  expect_num_args 1 "$@"
  local dependency_name=$1

  set +u
  local FILENAME=${TP_NAME_TO_ARCHIVE_NAME[$dependency_name]}
  set -u
  if [[ -z $FILENAME ]]; then
    fatal "Third-party dependency '$dependency_name' not found in TP_NAME_TO_ARCHIVE_NAME"
  fi
  local download_url=${TP_NAME_TO_URL[$dependency_name]:-${CLOUDFRONT_URL_PREFIX}/${FILENAME}}

  # If download_url is "mkdir" then we just create empty directory with specified name.
  if [[ $download_url != "mkdir" ]]; then
    ensure_file_downloaded "$download_url" "$FILENAME"
    extract_archive "$FILENAME" "$TP_SOURCE_DIR"
  else
    log "Creating directory: $TP_SOURCE_DIR/$FILENAME"
    mkdir -p "$TP_SOURCE_DIR/$FILENAME"
  fi

  set +u
  local extra_num=${TP_NAME_TO_EXTRA_NUM[$dependency_name]}
  log "Number of extra downloads for third-party dependency '$dependency_name': ${extra_num}"
  set -u
  if [[ $extra_num -gt 0 ]]; then
    log_empty_line
    for ((i=1; i<=$extra_num; i++)); do
      local key="${dependency_name}_${i}"
      local extra_url=${TP_NAME_TO_EXTRA_URL[$key]}
      local extra_archive_name=${TP_NAME_TO_EXTRA_ARCHIVE_NAME[$key]}
      local extra_dir=${TP_NAME_TO_EXTRA_DIR[$key]}
      local extra_post_exec=${TP_NAME_TO_EXTRA_POST_EXEC[$key]}
      log "Fetching extra urls: ${extra_archive_name}"
      ensure_file_downloaded "${extra_url}" "${extra_archive_name}"
      extract_archive "${extra_archive_name}" "${TP_SOURCE_DIR}/${extra_dir}"
      pushd "${TP_SOURCE_DIR}/${extra_dir}"
      eval $extra_post_exec
      popd
    done
    log_empty_line
  fi
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
  # We have to set TP_DIR based on DEST_DIR, because we set a lot of variables based on TP_DIR.
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
  fetch_and_expand glog

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
  fetch_and_expand gmock
fi

if [ ! -d "$GFLAGS_DIR" ]; then
  fetch_and_expand gflags
fi

if [ ! -d "$LLVM_DIR" ]; then
  fetch_and_expand llvm
fi

# Check that the gperftools patch has been applied.
# If you add or remove patches, bump the patchlevel below to ensure
# that any new Jenkins builds pick up your patches.
GPERFTOOLS_PATCHLEVEL=3
delete_if_wrong_patchlevel "$GPERFTOOLS_DIR" "$GPERFTOOLS_PATCHLEVEL"
if [ ! -d "$GPERFTOOLS_DIR" ]; then
  fetch_and_expand gperftools

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
  fetch_and_expand protobuf
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$PROTOBUF_DIR"
    run_autoreconf -fvi
    popd
  fi
fi

SNAPPY_PATCHLEVEL=1
delete_if_wrong_patchlevel "$SNAPPY_DIR" "$SNAPPY_PATCHLEVEL"
if [ ! -d "$SNAPPY_DIR" ]; then
  fetch_and_expand snappy
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$SNAPPY_DIR"
    patch -p1 < "$TP_DIR"/patches/snappy-define-guard-macro.patch
    touch patchlevel-"$SNAPPY_PATCHLEVEL"
    run_autoreconf -fvi
    popd
  fi
fi

if [ ! -d "$ZLIB_DIR" ]; then
  fetch_and_expand zlib
fi

if [ ! -d "$LIBEV_DIR" ]; then
  fetch_and_expand libev
fi

if [ ! -d "$RAPIDJSON_DIR" ]; then
  fetch_and_expand rapidjson
fi

if [ ! -d "$SQUEASEL_DIR" ]; then
  fetch_and_expand squeasel
fi

if [ ! -d "$GCOVR_DIR" ]; then
  fetch_and_expand gcovr
fi

if [ ! -d "$CURL_DIR" ]; then
  fetch_and_expand curl
fi

CRCUTIL_PATCHLEVEL=1
delete_if_wrong_patchlevel "$CRCUTIL_DIR" "$CRCUTIL_PATCHLEVEL"
if [ ! -d "$CRCUTIL_DIR" ]; then
  fetch_and_expand crcutil

  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$CRCUTIL_DIR"
    patch -p0 < "$TP_DIR/patches/crcutil-fix-libtoolize-on-osx.patch"
    touch "patchlevel-$CRCUTIL_PATCHLEVEL"
    popd
  fi
  echo
fi

if [ ! -d "$LIBUNWIND_DIR" ]; then
  fetch_and_expand libunwind
fi

if [[ "$OSTYPE" =~ ^linux ]] && [[ ! -d "$LIBCXX_DIR" ]]; then
  fetch_and_expand libcxx
fi

LZ4_PATCHLEVEL=1
delete_if_wrong_patchlevel "$LZ4_DIR" "$LZ4_PATCHLEVEL"
if [ ! -d "$LZ4_DIR" ]; then
  fetch_and_expand lz4
  if [ "$DOWNLOAD_ONLY" == "0" ]; then
    pushd "$LZ4_DIR"
    patch -p1 < $TP_DIR/patches/lz4-0001-Fix-cmake-build-to-use-gnu-flags-on-clang.patch
    touch patchlevel-$LZ4_PATCHLEVEL
    popd
  fi
  echo
fi

if [ ! -d "$BITSHUFFLE_DIR" ]; then
  fetch_and_expand bitshuffle
fi

if is_linux && [[ ! -d $NVML_DIR ]]; then
  fetch_and_expand nvml
fi

if is_linux && [[ ! -d $LIBBACKTRACE_DIR ]]; then
  fetch_and_expand libbacktrace
fi

if [[ ! -d $CQLSH_DIR ]]; then
  fetch_and_expand cqlsh
fi

if [[ ! -d $CRYPT_BLOWFISH_DIR ]]; then
  fetch_and_expand crypt_blowfish
fi

if [[ ! -d $REDIS_DIR ]]; then
  fetch_and_expand redis_cli
fi

if [[ ! -d $TACOPIE_DIR ]]; then
  fetch_and_expand tacopie
fi

if [[ ! -d $CPP_REDIS_DIR ]]; then
  fetch_and_expand cpp_redis
fi

echo "---------------"
if [[ $DOWNLOAD_ONLY -eq 1 ]]; then
  log "Sources of thirdparty dependencies downloaded successfully into $TP_DOWNLOAD_DIR"
else
  log "Sources of thirdparty dependencies downloaded and extracted into $TP_SOURCE_DIR"
fi
