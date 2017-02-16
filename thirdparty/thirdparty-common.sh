# Copyright (c) YugaByte, Inc.

. "${BASH_SOURCE%/*}/../build-support/common-build-env.sh"

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

# Portions Copyright (c) YugaByte, Inc.

if [[ -n ${_THIRDPARTY_COMMON_SOURCED:-} ]]; then
  return
fi

_THIRDPARTY_COMMON_SOURCED=1

TP_BUILD_DIR=$YB_THIRDPARTY_DIR/build
TP_DOWNLOAD_DIR=$YB_THIRDPARTY_DIR/download
TP_SOURCE_DIR=$YB_THIRDPARTY_DIR/src

# This URL corresponds to the CloudFront Distribution for the S3
# bucket cloudera-thirdparty-libs which is directly accessible at
# http://cloudera-thirdparty-libs.s3.amazonaws.com/
# TODO: copy this to YugaByte's own S3 bucket. Ideally use authenticated S3 for downloads.
CLOUDFRONT_URL_PREFIX=http://d3dr9sfxru4sde.cloudfront.net

PREFIX_COMMON=$YB_THIRDPARTY_DIR/installed/common
PREFIX_DEPS=$YB_THIRDPARTY_DIR/installed/uninstrumented
PREFIX_DEPS_TSAN=$YB_THIRDPARTY_DIR/installed/tsan

# libstdcxx needs its own prefix so that it is not inadvertently included in the library search path
# during non-TSAN builds.
PREFIX_LIBSTDCXX=$PREFIX_DEPS/gcc
PREFIX_LIBSTDCXX_TSAN=$PREFIX_DEPS_TSAN/gcc

BUILD_STAMP_DIR=$YB_THIRDPARTY_DIR/build-status

# Come up with a string that allows us to tell when to rebuild a particular third-party dependency.
# The result is returned in the get_build_stamp_for_component_rv variable, which should have been
# made local by the caller.
get_build_stamp_for_component() {
  expect_num_args 1 "$@"
  local component_name=$1

  # The set of input files
  local input_files_for_stamp=(
    build-thirdparty.sh
    thirdparty-common.sh
  )
  input_files_for_stamp+=( "build-definitions/build_${component_name}.sh" )

  local relative_path
  for relative_path in "${input_files_for_stamp[@]}"; do
    local abs_path=$YB_THIRDPARTY_DIR/$relative_path
    if [[ ! -f $abs_path ]]; then
      fatal "File '$abs_path' does not exist -- expecting it to exist when creating a 'stamp' " \
            "for the build configuration of '$component_name'. Current directory: $PWD."
    fi
  done

  local git_commit_sha1=$(
    cd "$YB_THIRDPARTY_DIR" && git log --pretty=%H -n 1 "${input_files_for_stamp[@]}"
  )
  local git_diff_sha256=$(
    ( cd "$YB_THIRDPARTY_DIR" && git diff "${input_files_for_stamp[@]}" ) | compute_sha256sum
  )
  get_build_stamp_for_component_rv="git_commit_sha1=$git_commit_sha1; "
  get_build_stamp_for_component_rv+=" git_diff_sha256=$git_diff_sha256"
}

# Get build stamp path for the given component and "install prefix type" (one of "common",
# "uninstrumented", "tsan"). The result is stored in the get_build_stamp_path_for_component_rv
# variable, which should have been made local by the caller.
get_build_stamp_path_for_component() {
  expect_num_args 1 "$@"
  local component_name=$1
  local result=$TP_BUILD_DIR/$install_prefix_type/.build-stamp-$component_name
  get_build_stamp_path_for_component_rv=$result
}

# Determines if we should rebuild a component with the given name based on the existing "stamp" file
# and the current value of the "stamp" (based on Git SHA1 and local changes) for the component.  The
# result is returned in should_rebuild_component_rv variable, which should have been made local by
# the caller.
should_rebuild_component() {
  expect_num_args 1 "$@"
  local component_name=$1

  local get_build_stamp_path_for_component_rv
  get_build_stamp_path_for_component "$component_name"
  local build_stamp_path=$get_build_stamp_path_for_component_rv

  local old_build_stamp="N/A"
  if [[ -f $build_stamp_path ]]; then
    old_build_stamp=$(<"$build_stamp_path")
  fi

  local get_build_stamp_for_component_rv
  get_build_stamp_for_component "$component_name"
  local new_build_stamp=$get_build_stamp_for_component_rv

  if [[ $old_build_stamp == $new_build_stamp ]]; then
    log "Not rebuilding $component_name ($install_prefix_type) -- nothing changed."
    should_rebuild_component_rv=false
  else
    log "Have to rebuild $component_name ($install_prefix_type):"
    log "Old build stamp for $component_name: $old_build_stamp (from $build_stamp_path)"
    log "New build stamp for $component_name: $new_build_stamp"
    should_rebuild_component_rv=true
  fi
}

save_build_stamp_for_component() {
  expect_num_args 1 "$@"
  local component_name=$1

  local get_build_stamp_path_for_component_rv
  get_build_stamp_path_for_component "$component_name"
  build_stamp_path=$get_build_stamp_path_for_component_rv

  local get_build_stamp_for_component_rv
  get_build_stamp_for_component "$component_name"
  local new_build_stamp=$get_build_stamp_for_component_rv

  log "Saving new build stamp to '$build_stamp_path': $new_build_stamp"
  echo "$new_build_stamp" >"$build_stamp_path"
}

# Save the current build environment.
save_env() {
  _PREFIX=${PREFIX}
  _EXTRA_CFLAGS=${EXTRA_CFLAGS}
  _EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS}
  _EXTRA_LDFLAGS=${EXTRA_LDFLAGS}
  _EXTRA_LIBS=${EXTRA_LIBS}
}

# Restore the most recently saved build environment.
restore_env() {
  PREFIX=${_PREFIX}
  EXTRA_CFLAGS=${_EXTRA_CFLAGS}
  EXTRA_CXXFLAGS=${_EXTRA_CXXFLAGS}
  EXTRA_LDFLAGS=${_EXTRA_LDFLAGS}
  EXTRA_LIBS=${_EXTRA_LIBS}
}

get_build_directory() {
  expect_num_args 1 "$@"
  local basename=$1
  echo $TP_BUILD_DIR/$install_prefix_type/$basename
}

remove_cmake_cache() {
  rm -rf CMakeCache.txt CMakeFiles/
}

create_build_dir_and_prepare() {
  if [[ $# -lt 1 || $# -gt 2 ]]; then
    fatal "$FUNCNAME expects either one or two arguments: source directory and optionally" \
          "the directory within the source directory to run the build in."
  fi
  local src_dir=$1
  local src_dir_basename=${src_dir##*/}
  local rel_build_dir=${2:-}

  if [[ ! -d $src_dir ]]; then
    fatal "Directory '$src_dir' does not exist"
  fi

  if [[ -n $rel_build_dir ]]; then
    rel_build_dir="/$rel_build_dir"
  fi

  src_dir=$( cd "$src_dir" && pwd )

  local src_dir_basename=${src_dir##*/}
  local build_dir=$( get_build_directory "$src_dir_basename" )
  if [[ -z $build_dir ]]; then
    fatal "Failed to set build directory for '$src_dir_basename'."
  fi
  local build_run_dir=$build_dir$rel_build_dir
  if [[ ! -d $build_dir ]]; then
    if [[ $src_dir_basename =~ ^llvm- ]]; then
      log "$src_dir_basename is a CMake project with an out-of-source build, no need to copy" \
          "sources anywhere. Will let the build_llvm function take care of creating the build" \
          "directory."
      return
    elif [[ $src_dir_basename =~ ^gcc- ]]; then
      log "$src_dir_basename is using an out-of-source build. Simply creating an empty directory."
      mkdir -p "$build_dir"
    else
      log "$build_dir does not exist, bootstrapping it from $src_dir"
      mkdir -p "$build_dir"
      rsync -a "$src_dir/" "$build_dir"
      (
        cd "$build_run_dir"
        log "Running 'make distclean' and 'autoreconf' and removing CMake cache files in $PWD," \
            "ignoring errors."
        (
          set -x +e
          # Ignore errors here, because not all projects are autotools projects.
          make distclean
          autoreconf --force --verbose --install
          remove_cmake_cache
          exit 0
        )
        log "Finished running 'make distclean' and 'autoreconf' and removing CMake cache."
      )
    fi
  fi
  log "Running build in $build_run_dir"
  cd "$build_run_dir"
}

run_make() {
  (
    set -x
    make -j"$YB_NUM_CPUS" "$@"
  )
}

# Source scripts called build_<component_name>.sh inside thirdparty/build-definitions. This function
# only needs to be called once and can be deleted afterwards.
source_thirdparty_build_definitions() {
  local build_def_file
  for build_def_file in "$YB_THIRDPARTY_DIR"/build-definitions/build_*.sh; do
    . "$build_def_file"
  done
}

# -------------------------------------------------------------------------------------------------
# Initialization

detect_num_cpus
source_thirdparty_build_definitions
# We don't need this function anymore after calling it once, so delete it.
unset -f source_thirdparty_build_definitions
