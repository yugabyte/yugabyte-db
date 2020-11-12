#!/usr/bin/env bash

set -euo pipefail

echo "Build script $BASH_SOURCE is running"

. "${BASH_SOURCE%/*}/../common-test-env.sh"

readonly COMMON_YB_BUILD_ARGS_FOR_CPP_BUILD=(
  --no-rebuild-thirdparty
  --skip-java
)

# =================================================================================================
# Main script
# =================================================================================================

cd "$YB_SRC_ROOT"

export YB_RUN_JAVA_TEST_METHODS_SEPARATELY=1

if is_mac; then
  # This is needed to make sure we're using Homebrew-installed CMake on Mac OS X.
  export PATH=/usr/local/bin:$PATH
fi

BUILD_TYPE=${BUILD_TYPE:-debug}
build_type=$BUILD_TYPE
normalize_build_type
readonly build_type

BUILD_TYPE=$build_type
readonly BUILD_TYPE
export BUILD_TYPE

set_build_root
set_common_test_paths

readonly BUILD_ROOT
export BUILD_ROOT

YB_COMPILE_ONLY=${YB_COMPILE_ONLY:-0}

TEST_LOG_DIR="$BUILD_ROOT/test-logs"
TEST_TMP_ROOT_DIR="$BUILD_ROOT/test-tmp"

FAILURES=""

cd $BUILD_ROOT

export YB_GZIP_PER_TEST_METHOD_LOGS=1
export YB_GZIP_TEST_LOGS=1
export YB_DELETE_SUCCESSFUL_PER_TEST_METHOD_LOGS=1

if [[ $YB_COMPILE_ONLY != "1" ]]; then
    if [[ $YB_BUILD_JAVA == "1" ]]; then
      log "Running Java tests in a non-distributed way"
      if ! time run_all_java_test_methods_separately; then
        EXIT_STATUS=1
        FAILURES+=$'Java tests failed\n'
      fi
      log "Finished running Java tests (see timing information above)"
    fi
fi

if [[ -n $FAILURES ]]; then
  heading "Failure summary"
  echo >&2 "$FAILURES"
fi

exit $EXIT_STATUS
