#!/bin/bash

#
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
#
set -euo pipefail

# shellcheck source=build-support/common-test-env.sh
. "${0%/*}/../common-test-env.sh"
# shellcheck source=build-support/digest_package.sh
. "${YB_SRC_ROOT}/build-support/digest_package.sh"


print_help() {
  cat <<-EOT
Usage: ${0##*} <options>
Options:
  -h, --help
    Show help

Environment variables:
  JOB_NAME
    Jenkins job name.
  BUILD_TYPE
    Passed directly to build-and-test.sh. The default value is determined based on the job name
    if this environment variable is not specified or if the value is "auto".
EOT
}

echo "Jenkins Build script ${BASH_SOURCE[0]} is running"

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
done

JOB_NAME=${JOB_NAME:-}

export YB_BUILD_JAVA=${YB_BUILD_JAVA:-1}
export YB_BUILD_CPP=${YB_BUILD_CPP:-1}
export YB_PG_PARQUET_DEBUG_CLEAN=1

readonly COMMON_YB_BUILD_ARGS_FOR_CPP_BUILD=(
  --no-rebuild-thirdparty
  --skip-java
)

# -------------------------------------------------------------------------------------------------
# Functions

build_cpp_code() {
  # Save the source root just in case, but this should not be necessary as we will typically run
  # this function in a separate process in case it is building code in a non-standard location
  # (i.e. in a separate directory where we rollback the last commit for regression tracking).
  local old_yb_src_root=$YB_SRC_ROOT

  expect_num_args 1 "$@"
  set_yb_src_root "$1"

  heading "Building C++ code in $YB_SRC_ROOT."

  remote_opt=""
  if [[ ${YB_REMOTE_COMPILATION:-} == "1" ]]; then
    # This helps with our background script resizing the build cluster, because it looks at all
    # running build processes with the "--remote" option as of 08/2017.
    remote_opt="--remote"
  fi

  # Delegate the actual C++ build to the yb_build.sh script.
  #
  # We're explicitly disabling third-party rebuilding here as we've already built third-party
  # dependencies (or downloaded them, or picked an existing third-party directory) above.

  local yb_build_args=(
    "${COMMON_YB_BUILD_ARGS_FOR_CPP_BUILD[@]}"
    "${BUILD_TYPE}"
  )

  # Static check of bash scripts need only be done during one phase of the build.
  yb_build_args+=( "--shellcheck" )

  if [[ -n "${YB_PGO_DATA_FILE:-}" && -f "$YB_PGO_DATA_FILE" ]]; then
    yb_build_args+=( "--pgo-data-path=$YB_PGO_DATA_FILE" )
  fi

  log "Building cpp code with options: ${yb_build_args[*]}"

  time "$YB_SRC_ROOT/yb_build.sh" ${remote_opt} "${yb_build_args[@]}"

  log "Finished building C++ code (see timing information above)"

  # Restore the old source root. See the comment at the top.
  set_yb_src_root "$old_yb_src_root"
}

# =================================================================================================
# Main script
# =================================================================================================

log "Running with Bash version $BASH_VERSION"

cd "$YB_SRC_ROOT"
if ! "$YB_BUILD_SUPPORT_DIR/common-build-env-test.sh"; then
  fatal "Test of the common build environment failed, cannot proceed."
fi

log "Removing old JSON-based test report files"
(
  set -x
  find . -name "*_test_report.json" -exec rm -f '{}' \;
  rm -f test_results.json test_failures.json
)

log "Deleting branches starting with 'arcpatch-D'"
current_branch=$( git rev-parse --abbrev-ref HEAD )
for branch_name in $( git for-each-ref --format="%(refname)" refs/heads/ ); do
  branch_name=${branch_name#refs/heads/}
  if [[ "$branch_name" =~ ^arcpatch-D ]]; then
    if [ "$branch_name" == "$current_branch" ]; then
      echo "'$branch_name' is the current branch, not deleting."
    else
      ( set -x; git branch -D "$branch_name" )
    fi
  fi
done

# -------------------------------------------------------------------------------------------------
# Environmental checks
echo
echo ----------------------------------------------------------------------------------------------
echo "ifconfig (without the 127.0.x.x IPs)"
echo ----------------------------------------------------------------------------------------------
echo

set +e
ifconfig | grep -vE "inet 127[.]0[.]"
set -e

echo
echo ----------------------------------------------------------------------------------------------
echo

echo "Max number of open files:"
ulimit -n
echo

show_disk_usage

if is_mac; then
  "$YB_SCRIPT_PATH_KILL_LONG_RUNNING_MINICLUSTER_DAEMONS"
fi

# -------------------------------------------------------------------------------------------------
YB_VERBOSE=true activate_virtualenv
set_pythonpath

# -------------------------------------------------------------------------------------------------
# Build root setup
# -------------------------------------------------------------------------------------------------
# shellcheck source=build-support/jenkins/common-lto.sh
. "${BASH_SOURCE%/*}/common-lto.sh"
log "Setting build_root"

# shellcheck disable=SC2119
set_build_root

log "BUILD_ROOT: ${BUILD_ROOT}"

set_common_test_paths

# Double-check that any previous build artifacts have not been left around.
if [[ -L ${BUILD_ROOT} ]]; then
  # If the build root is a symlink, we have to find out what it is pointing to and delete that
  # directory as well.
  build_root_real_path=$( readlink "${BUILD_ROOT}" )
  log "BUILD_ROOT ('${BUILD_ROOT}') is a symlink to '${build_root_real_path}'"
  rm -rf "${build_root_real_path}"
  unlink "${BUILD_ROOT}"
else
  log "Deleting BUILD_ROOT ('$BUILD_ROOT')."
  ( set -x; rm -rf "$BUILD_ROOT" )
fi

log "Deleting yb-test-logs from all subdirectories of ${YB_BUILD_PARENT_DIR} so that Jenkins " \
    "does not get confused with old JUnit-style XML files."
( set -x; rm -rf "$YB_BUILD_PARENT_DIR"/*/yb-test-logs )

log "Deleting old packages from '${YB_BUILD_PARENT_DIR}'"
( set -x; rm -rf "${YB_BUILD_PARENT_DIR}/yugabyte-"*"-$build_type-"*".tar.gz" )

mkdir_safe "${BUILD_ROOT}"

readonly BUILD_ROOT
export BUILD_ROOT

# -------------------------------------------------------------------------------------------------
# End of build root setup
# -------------------------------------------------------------------------------------------------

export YB_SKIP_LLVM_TOOLCHAIN_SYMLINK_CREATION=1

# We need to set this prior to the first invocation of yb_build.sh.
export YB_SKIP_FINAL_LTO_LINK=1

"${YB_SRC_ROOT}/yb_build.sh" --cmake-unit-tests

find_or_download_ysql_snapshots
find_or_download_thirdparty
validate_thirdparty_dir
detect_toolchain
log_thirdparty_and_toolchain_details
find_make_or_ninja_and_update_cmake_opts

log "YB_USE_NINJA=$YB_USE_NINJA"
log "YB_NINJA_PATH=${YB_NINJA_PATH:-undefined}"

set_java_home

log "Running with PATH: ${PATH}"

log "Running Python tests"
time run_python_tests
log "Finished running Python tests (see timing information above)"

log "Running a light-weight lint script on our Java code"
time lint_java_code
log "Finished running a light-weight lint script on the Java code"

export YB_SKIP_BUILD=${YB_SKIP_BUILD:-0}

YB_SKIP_CPP_COMPILATION=${YB_SKIP_CPP_COMPILATION:-0}

export NO_REBUILD_THIRDPARTY=1

THIRDPARTY_BIN=$YB_SRC_ROOT/thirdparty/installed/bin
export PPROF_PATH=$THIRDPARTY_BIN/pprof

# Configure the build

cd "$BUILD_ROOT"

# We have a retry loop around CMake because it sometimes fails due to NFS unavailability.
declare -i -r MAX_CMAKE_RETRIES=3
declare -i cmake_attempt_index=1
while true; do
  if "${YB_SRC_ROOT}/yb_build.sh" "${BUILD_TYPE}" --cmake-only --no-remote; then
    log "CMake succeeded after attempt $cmake_attempt_index"
    break
  fi
  if [[ $cmake_attempt_index -eq ${MAX_CMAKE_RETRIES} ]]; then
    fatal "CMake failed after ${MAX_CMAKE_RETRIES} attempts, giving up."
  fi
  heading "CMake failed at attempt $cmake_attempt_index, re-trying"
  (( cmake_attempt_index+=1 ))
done

detect_num_cpus

declare -i EXIT_STATUS=0

FAILURES=""

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=1

# -------------------------------------------------------------------------------------------------
# Build C++ code regardless of YB_BUILD_CPP, because we'll also need it for Java tests.
# -------------------------------------------------------------------------------------------------

heading "Building C++ code"

build_cpp_code "$YB_SRC_ROOT"

log "Disk usage after C++ build:"
show_disk_usage

# We can grep for this line in the log to determine the stage of the build job.
log "ALL OF YUGABYTE C++ BUILD FINISHED"

# -------------------------------------------------------------------------------------------------
# End of the C++ code build, except maybe the final LTO linking step.
# -------------------------------------------------------------------------------------------------

# -------------------------------------------------------------------------------------------------
# Running initdb
# -------------------------------------------------------------------------------------------------

export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=0

if ! is_tsan ; then
  declare -i initdb_attempt_index=1
  declare -i -r MAX_INITDB_ATTEMPTS=3

  while [[ $initdb_attempt_index -le ${MAX_INITDB_ATTEMPTS} ]]; do
    log "Creating initial system catalog snapshot (attempt $initdb_attempt_index)"
    if ! time "${YB_SRC_ROOT}/yb_build.sh" "${BUILD_TYPE}" initdb --skip-java; then
      initdb_err_msg="Failed to create initial sys catalog snapshot at "
      initdb_err_msg+="attempt ${initdb_attempt_index}"
      log "${initdb_err_msg}. PostgreSQL tests may take longer."
      FAILURES+="$initdb_err_msg"$'\n'
      EXIT_STATUS=1
    else
      log "Successfully created initial system catalog snapshot at attempt ${initdb_attempt_index}."
      break
    fi
    (( initdb_attempt_index+=1 ))
  done
  if [[ ${initdb_attempt_index} -gt ${MAX_INITDB_ATTEMPTS} ]]; then
    fatal "Failed to run create initial sys catalog snapshot after ${MAX_INITDB_ATTEMPTS} attempts."
  fi
fi


# -------------------------------------------------------------------------------------------------
# Final LTO linking
# -------------------------------------------------------------------------------------------------

export YB_SKIP_FINAL_LTO_LINK=0
if [[ ${YB_LINKING_TYPE} == *-lto ]]; then
  yb_build_cmd_line_for_lto=(
    "${YB_SRC_ROOT}/yb_build.sh"
    "${BUILD_TYPE}" --skip-java --force-run-cmake
  )

  if [[ -n "${YB_PGO_DATA_FILE:-}" && -f "$YB_PGO_DATA_FILE" ]]; then
    yb_build_cmd_line_for_lto+=( "--pgo-data-path=$YB_PGO_DATA_FILE" )
  fi

  if [[ $( grep -E 'MemTotal: .* kB' /proc/meminfo ) =~ ^.*\ ([0-9]+)\ .*$ ]]; then
    total_mem_kb=${BASH_REMATCH[1]}
    gib_per_lto_process=17
    yb_build_parallelism_for_lto=$(( total_mem_kb / (gib_per_lto_process * 1024 * 1024) ))
    if [[ ${yb_build_parallelism_for_lto} -lt 1 ]]; then
      yb_build_parallelism_for_lto=1
    fi
    log "Total memory size: ${total_mem_kb} KB," \
        "using LTO linking parallelism ${yb_build_parallelism_for_lto} based on " \
        "at least ${gib_per_lto_process} GiB per LTO process."
  else
    log "Warning: could not determine total amount of memory, using parallelism of 1 for LTO."
    log "Contents of /proc/meminfo:"
    set +e
    cat /proc/meminfo >&2
    set -e
    yb_build_parallelism_for_lto=1
  fi
  yb_build_cmd_line_for_lto+=( "-j${yb_build_parallelism_for_lto}" )

  log "Performing final LTO linking"
  ( set -x; "${yb_build_cmd_line_for_lto[@]}" )
fi

# -------------------------------------------------------------------------------------------------
# Java build
# -------------------------------------------------------------------------------------------------

export YB_MVN_LOCAL_REPO=$BUILD_ROOT/m2_repository

java_build_failed=false
if [[ ${YB_BUILD_JAVA} == "1" && ${YB_SKIP_BUILD} != "1" ]]; then
  set_mvn_parameters

  # We need a truststore for the CA used in unit tests, only for Java tests, so we generate it here.
  "${YB_SRC_ROOT}/build-support/generate_test_truststore.sh" "$BUILD_ROOT/test_certs"

  heading "Building Java code..."
  if [[ -n ${JAVA_HOME:-} ]]; then
    export PATH=${JAVA_HOME}/bin:${PATH}
  fi

  heading "Java 'clean' build is complete, will now actually build Java code"

  for java_project_dir in "${yb_java_project_dirs[@]}"; do
    pushd "${java_project_dir}"
    heading "Building Java code in directory '${java_project_dir}'"
    if ! build_yb_java_code_with_retries -DskipTests clean install; then
      EXIT_STATUS=1
      FAILURES+="Java build failed in directory '${java_project_dir}'"$'\n'
      java_build_failed=true
    else
      log "Java code build in directory '${java_project_dir}' SUCCEEDED"
    fi
    popd +0
  done

  if [[ ${java_build_failed} == "true" ]]; then
    fatal "Java build failed, stopping here."
  fi

  heading "Running a test locally to force Maven to download all test-time dependencies"
  (
    cd "${YB_SRC_ROOT}/java"
    build_yb_java_code test \
                       -Dtest=org.yb.client.TestTestUtils#testDummy \
                       "${MVN_OPTS_TO_DOWNLOAD_ALL_DEPS[@]}"
  )
  heading "Finished running a test locally to force Maven to download all test-time dependencies"

  # Tell gen_version_info.py to store the Git SHA1 of the commit really present in the code
  # being built, not our temporary commit to update pom.xml files.
  get_current_git_sha1
  export YB_VERSION_INFO_GIT_SHA1=$current_git_sha1

  collect_java_tests

  log "Finished building Java code (see timing information above)"
fi

# -------------------------------------------------------------------------------------------------
if [[ -n ${FAILURES} ]]; then
  heading "Failure summary"
  echo >&2 "${FAILURES}"
fi

exit ${EXIT_STATUS}
