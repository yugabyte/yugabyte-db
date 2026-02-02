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


echo "Jenkins Build script ${BASH_SOURCE[0]} is running"

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

# -------------------------------------------------------------------------------------------------
# Now that all C++ and Java code has been built, test creating a package.
#
# Skip this in ASAN/TSAN, as there are still unresolved issues with dynamic libraries there
# (conflicting versions of the same library coming from thirdparty vs. Linuxbrew) as of 12/04/2017.

if [[ ${YB_SKIP_CREATING_RELEASE_PACKAGE:-} != "1" ]] && ! is_sanitizer ; then
  heading "Creating a distribution package"

  package_path_file="${BUILD_ROOT}/package_path.txt"
  rm -f "${package_path_file}"

  # We are passing --build_args="--skip-build" using the "=" syntax, because otherwise it would be
  # interpreted as an argument to yb_release.py, causing an error.
  #
  # Everything has already been built by this point, so there is no need to invoke compilation at
  # all as part of building the release package.
  current_git_commit=$(git rev-parse HEAD)
  yb_release_cmd=(
    "${YB_SRC_ROOT}/yb_release"
    --build "${build_type}"
    --build_root "${BUILD_ROOT}"
    "--build_args=--skip-build"
    --save_release_path_to_file "${package_path_file}"
    --commit "${current_git_commit}"
    --force
  )

  if [[ ${YB_BUILD_YW:-0} == "1" ]]; then
    # This is needed for build.sbt to use YB Client jars that we've built and installed to
    # YB_MVN_LOCAL_REPO.
    export USE_MAVEN_LOCAL=true
    yb_release_cmd+=( --yw )
  fi

  (
    set -x
    time "${yb_release_cmd[@]}"
  )

  YB_PACKAGE_PATH=$( cat "${package_path_file}" )
  if [[ -z ${YB_PACKAGE_PATH} ]]; then
    fatal "File '${package_path_file}' is empty"
  fi
  if [[ ! -f ${YB_PACKAGE_PATH} ]]; then
    fatal "Package path stored in '${package_path_file}' does not exist: ${YB_PACKAGE_PATH}"
  fi

  # Digest the package.
  digest_package "${YB_PACKAGE_PATH}"

else
  log "Skipping creating distribution package. Build type: $build_type, OSTYPE: ${OSTYPE}," \
      "YB_SKIP_CREATING_RELEASE_PACKAGE: ${YB_SKIP_CREATING_RELEASE_PACKAGE:-undefined}."

  # yugabyted-ui is usually built during package build.  Test yugabyted-ui build here when not
  # building package.
  log "Building yugabyted-ui"
  time "${YB_SRC_ROOT}/yb_build.sh" "${BUILD_TYPE}" --build-yugabyted-ui --skip-java
fi

# -------------------------------------------------------------------------------------------------
YB_VERBOSE=true activate_virtualenv
set_pythonpath

heading "Dependency Graph Self-Test"
( set -x
  "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH" \
    --build-root "${BUILD_ROOT}" \
    self-test \
    --rebuild-graph )

# -------------------------------------------------------------------------------------------------
if [[ "${YB_COMPILE_ONLY}" == "1" ]]; then
  heading "Skipping Prep for DB unit testing."
else
  heading "Build Prep for DB unit testing."
  prep_ybc_testing
  export YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY:-0}
  log "YB_RUN_AFFECTED_TESTS_ONLY=${YB_RUN_AFFECTED_TESTS_ONLY}"
  if [[ ${YB_RUN_AFFECTED_TESTS_ONLY} == "1" ]]; then
    log "Running dependency graph to find tests based on modified files"
    if [[ -n "${YB_TEST_EXECUTION_FILTER_RE:-}" ]]; then
      log "Disregarding YB_TEST_EXECUTION_FILTER_RE for default test_conf.json"
      unset YB_TEST_EXECUTION_FILTER_RE
    fi
    # YB_GIT_COMMIT_FOR_DETECTING_TESTS allows overriding the commit to use to detect the set
    # of tests to run. Useful when testing this script.
    current_git_commit=$(git rev-parse HEAD)
    ( set -x
      "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH" \
          --build-root "${BUILD_ROOT}" \
          --git-commit "${YB_GIT_COMMIT_FOR_DETECTING_TESTS:-$current_git_commit}" \
          --output-test-config "${BUILD_ROOT}/test_conf.json" \
          affected
    )
  else
    log "Skipping dependency graph -- expecting to run all tests."
  fi
  heading "Preparing spark archive"
  prep_spark_archive
fi

exit 0
