# Copyright (c) YugaByte, Inc.

#@IgnoreInspection BashAddShebang

# Common bash code for test scripts/

if [[ $BASH_SOURCE == $0 ]]; then
  echo "$BASH_SOURCE must be sourced, not executed" >&2
  exit 1
fi

. "${BASH_SOURCE%/*}/common-build-env.sh"

NON_GTEST_TESTS_RE=$( regex_from_list "
  client_samples-test
  client_symbol-test
  merge-test
")

NON_GTEST_ROCKSDB_TESTS_RE=$( regex_from_list "
  c_test
  compact_on_deletion_collector_test
  db_sanity_test
  merge_test
  stringappend_test
")

# There gtest suites have internal dependencies between tests, so those tests can't be run
# separately.
TEST_BINARIES_TO_RUN_AT_ONCE_RE=$( regex_from_list "
  bin/tablet_server-test
  rocksdb-build/backupable_db_test
  rocksdb-build/thread_local_test
")

VALID_TEST_BINARY_DIRS=(
  bin
  rocksdb-build
)
VALID_TEST_BINARY_DIRS_RE=$( regex_from_list "${VALID_TEST_BINARY_DIRS[@]}" )

# gdb command to print a backtrace from a core dump. Taken from:
# http://www.commandlinefu.com/commands/view/6004/print-stack-trace-of-a-core-file-without-needing-to-enter-gdb-interactively

GDB_CORE_BACKTRACE_CMD_PREFIX=( gdb -q -n -ex bt -batch )

DEFAULT_TEST_TIMEOUT_SEC=600
INCREASED_TEST_TIMEOUT_SEC=1200

# We grep for these log lines and show them in the main log on test failure. This regular expression
# is used with egrep.
readonly RELEVANT_LOG_LINES_RE=\
'^[[:space:]]*(Value of|Actual|Expected):|^Expected|^Failed|: Failure$'

# -------------------------------------------------------------------------------------------------
# Functions
# -------------------------------------------------------------------------------------------------

# Sanitize the given string so we can make it a path component.
sanitize_for_path() {
  echo "$1" | sed 's/\//__/g; s/[:.]/_/g;'
}

set_common_test_paths() {
  expect_num_args 0 "$@"
  readonly YB_TEST_LOG_ROOT_DIR="$BUILD_ROOT/yb-test-logs"
  mkdir_safe "$YB_TEST_LOG_ROOT_DIR"
}

validate_relative_test_binary_path() {
  expect_num_args 1 "$@"
  local rel_test_binary=$1
  if [[ ! $rel_test_binary =~ ^[a-zA-Z0-9_-]+/[a-zA-Z0-9_.-]+$ ]]; then
    fatal "Expected a relative test binary path to consist of two components separated by '/'." \
          "Got: '$rel_test_binary'."
  fi
  local rel_test_binary_dirname=${rel_test_binary%/*}
  if [[ ! $rel_test_binary_dirname =~ $VALID_TEST_BINARY_DIRS_RE ]]; then
    fatal "Expected the directory name a test binary is contained in to be one of the following: " \
          "$VALID_TEST_BINARY_DIRS. Relative test binary path: $rel_test_binary" >&2
  fi
}

create_abs_test_binary_path() {
  expect_num_args 1 "$@"
  local rel_test_binary_path=$1
  local abs_test_binary_path=$BUILD_ROOT/$rel_test_binary_path
  if [[ ! -f $abs_test_binary_path && -f "$abs_test_binary_path.sh" ]]; then
    abs_test_binary_path+=".sh"
  fi
  echo "$abs_test_binary_path"
}

validate_abs_test_binary_path() {
  if [[ ! -f $abs_test_binary_path ]]; then
    fatal "Test binary '$abs_test_binary_path' does not exist"
  fi
  if [[ ! -x $abs_test_binary_path ]]; then
    fatal "Test binary '$abs_test_binary_path' is not executable"
  fi
}

# Validates a "test descriptor" string. This is our own concept to identify tests within our test
# suite. A test descriptor has one of the two forms:
#   - <relative_test_binary>:::<test_name_within_binary>
#   - <relative_test_binary>
# We've chosen triple colon as a separator that would rarely occur otherwise.
# Examples:
#   - bin/tablet-test:::TestTablet/5.TestDeleteWithFlushAndCompact
#   - rocksdb-build/db_test:::DBTest.DBOpen_Change_NumLevels
#   - rocksdb-build/backupable_db_test
# The test name within the binary is what can be passed to Google Test as the --gtest_filter=...
# argument. Some test binaries have to be run at once, e.g. non-gtest test binary or
# test binaries with internal dependencies between tests. For those there is on ":::" separator
# or the <test_name_within_binary> part (e.g. rocksdb-build/backupable_db_test above).
validate_test_descriptor() {
  expect_num_args 1 "$@"
  local test_descriptor=$1
  if [[ $test_descriptor =~ ::: ]]; then
    if [[ $test_descriptor =~ :::.*::: ]]; then
      fatal "The ':::' separator occurs twice within the test descriptor: $test_descriptor"
    fi
    # Remove ::: and all that follows and get a relative binary path.
    validate_relative_test_binary_path "${test_descriptor%:::*}"
  else
    validate_relative_test_binary_path "$test_descriptor"
  fi
}

# Some tests are not based on the gtest framework and don't generate an XML output file.
# Also, RocksDB's thread_list_test is like that on Mac OS X.
is_known_non_gtest_test() {
  local test_name=$1
  local is_rocksdb=$2

  if [[ ! $is_rocksdb =~ ^[01]$ ]]; then
    fatal "The second argument to is_known_non_gtest_test (is_rocksdb) must be 0 or 1," \
          "was '$is_rocksdb'"
  fi

  if ( [ "$is_rocksdb" == "1" ] &&
       ( [[ "$test_name" =~ $NON_GTEST_ROCKSDB_TESTS_RE ]] ||
         ( [[ "$OSTYPE" =~ ^darwin ]] && [ "$test_name" == thread_list_test ] ) ) || \
       ( [ "$is_rocksdb" == "0" ] && [[ "$test_name" =~ $NON_GTEST_TESTS_RE ]] ) ); then
    return 0  # "true return value" in bash
  else
    return 1  # "false return value" in bash
  fi
}

# This is used by yb_test.sh.
# Takes relative test name such as "bin/client-test" or "rocksdb-build/db_test".
is_known_non_gtest_test_by_rel_path() {
  if [[ $# -ne 1 ]]; then
    fatal "is_known_non_gtest_test_by_rel_path takes exactly one argument" \
         "(test binary path relative to the build directory)"
  fi
  local rel_test_path=$1
  validate_relative_test_binary_path "$rel_test_path"

  local test_binary_basename=$( basename "$rel_test_path" )
  # Remove .sh extensions for bash-based tests.
  local test_binary_basename_no_ext=${test_binary_basename%[.]sh}
  local is_rocksdb=0
  if [[ "$rel_test_path" =~ ^rocksdb-build/ ]]; then
    is_rocksdb=1
  fi

  is_known_non_gtest_test "$test_binary_basename_no_ext" "$is_rocksdb"
}

# Collects Google Test tests from the given test binary.
# Input:
#   rel_test_binary
#     Test binary path relative to the build directory. Copied from the arguemnt.
# Output variables:
#   tests
#     This is an array that should be initialized by the caller. This adds new "test descriptors" to
#     this array.
#   num_binaries_to_run_at_once
#     The number of binaries to be run in one shot. This variable is incremented if it exists.
#   num_test_cases
#     Total number of test cases collected. Test cases are Google Test's way of grouping tests
#     (test functions) together. Usually they are methods of the same test class.
#     This variable is incremented if it exists.
#   num_tests
#     Total number of tests (test functions) collected. This variable is incremented if it exists.
collect_gtest_tests() {
  expect_num_args 0 "$@"

  validate_relative_test_binary_path "$rel_test_binary"

  if [ "$rel_test_binary" == "rocksdb-build/db_sanity_test" ]; then
    # db_sanity_check is not a test, but a command-line tool.
    return
  fi

  if is_known_non_gtest_test_by_rel_path "$rel_test_binary" || \
     [[ "$rel_test_binary" =~ $TEST_BINARIES_TO_RUN_AT_ONCE_RE ]]; then
    tests+=( "$rel_test_binary" )
    if [ -n "${num_binaries_to_run_at_once:-}" ]; then
      let num_binaries_to_run_at_once+=1
    fi
    return
  fi

  local gtest_list_stderr_path=$( mktemp -t "gtest_list_tests.stderr.XXXXXXXXXX" )

  local abs_test_binary_path=$( create_abs_test_binary_path "$rel_test_binary" )
  validate_abs_test_binary_path "$abs_test_binary_path"

  local gtest_list_tests_tmp_dir=$( mktemp -t -d "gtest_list_tests_tmp_dir.XXXXXXXXXX" )
  mkdir_safe "$gtest_list_tests_tmp_dir"  # on Mac OS X, mktemp does not create the directory
  local gtest_list_tests_cmd=( "$abs_test_binary_path" --gtest_list_tests )

  set +e
  pushd "$gtest_list_tests_tmp_dir" >/dev/null
  local gtest_list_tests_result  # this has to be on a separate line to capture the exit code
  gtest_list_tests_result=$(
    "${gtest_list_tests_cmd[@]}" 2>"$gtest_list_stderr_path"
  )
  gtest_list_tests_exit_code=$?
  popd >/dev/null
  set -e

  core_dir="$gtest_list_tests_tmp_dir"
  process_core_file
  rm -rf "$gtest_list_tests_tmp_dir"

  if [[ -z $gtest_list_tests_result ]]; then
    if [[ -s $gtest_list_stderr_path ]]; then
      log "Standard error from ${gtest_list_tests_cmd[@]}:"
      cat "$gtest_list_stderr_path"
    fi
    fatal "Empty output from ${gtest_list_tests_cmd[@]}," \
          "exit code: $gtest_list_tests_exit_code."
  fi

  # https://nixtricks.wordpress.com/2013/01/09/sed-delete-the-lines-lying-in-between-two-patterns/
  # Use the following command to delete the lines lying between PATTERN-1 and PATTERN-2, including
  # the lines containing these patterns:
  # sed '/PATTERN-1/,/PATTERN-2/d' input.txt
  sed '/^Suppressions used:$/,/^-\{53\}$/d' \
    <"$gtest_list_stderr_path" | sed '/^-\{53\}$/d; /^\s*$/d;' \
    >"$gtest_list_stderr_path.filtered"

  # -s tests if the given file is non-empty
  if [ -s "$gtest_list_stderr_path.filtered" ]; then
    (
      echo
      echo "'$abs_test_binary_path' produced non-empty stderr output when run with" \
           "--gtest_list_tests:"
      echo "[ ==== STDERR OUTPUT START ========================================================== ]"
      cat "$gtest_list_stderr_path.filtered"
      echo "[ ==== STDERR OUTPUT END ============================================================ ]"
      echo "Please add the test '$rel_test_binary' to the appropriate list in"
      echo "common-test-env.sh or fix the underlying issue in the code."
    ) >&2

    rm -f "$gtest_list_stderr_path" "$gtest_list_stderr_path.filtered"
    exit 1
  fi

  rm -f "$gtest_list_stderr_path"

  if [ "$gtest_list_tests_exit_code" -ne 0 ]; then
    echo "'$test_binary' does not seem to be a gtest test (--gtest_list_tests failed)" >&2
    echo "Please add this test to the appropriate blacklist in common-test-env.sh" >&2
    exit 1
  fi

  local test_list_item
  local IFS=$'\n'  # so that we can iterate through lines in $gtest_list_tests_result
  for test_list_item in $gtest_list_tests_result; do
    if [[ "$test_list_item" =~ ^\ \  ]]; then
      # This is a "test": an individual piece of test code to run, described by TEST or TEST_F
      # in a Google Test program.
      test=${test_list_item#  }  # Remove two leading spaces
      test=${test%%#*}  # Remove everything after a "#" comment
      test=${test%  }  # Remove two trailing spaces
      local full_test_name=$test_case$test
      if [[ -n ${total_num_tests:-} ]]; then
        let total_num_tests+=1
      fi
      if [[ -n ${YB_GTEST_REGEX:-} && ! $full_test_name =~ .*${YB_GTEST_REGEX:-}.* ]]; then
        log "Skipping test '$full_test_name' in ${rel_test_binary}: does not match the" \
            "regular expression '${YB_GTEST_REGEX}' (specified with YB_GTEST_REGEX)."
        if [[ -n ${num_tests_skipped:-} ]]; then
          let num_tests_skipped+=1
        fi
        continue
      fi
      tests+=( "${rel_test_binary}:::$test_case$test" )
      if [[ -n ${num_tests:-} ]]; then
        let num_tests+=1
      fi
    else
      # This is a "test case", a "container" for a number of tests, as in
      # TEST(TestCaseName, TestName). There is no executable item here.
      test_case=${test_list_item%%#*}  # Remove everything after a "#" comment
      test_case=${test_case%  }  # Remove two trailing spaces
      if [ -n "${num_test_cases:-}" ]; then
        let num_test_cases+=1
      fi
    fi
  done
}

# Set a number of variables in preparation to running a test.
# Inputs:
#   test_descriptor
# Outputs:
#   rel_test_binary
#     Relative path of the test binary
#   test_name
#     Test name within the test binary, to be passed to --gtest_filter=...
#   run_at_once
#     Whether we need to run all tests in this binary at once ("true" or "false")
#   what_test_str
#     A human-readable string describing what tests we are running within the test binary.
#   rel_test_log_path_prefix
#     A prefix of various log paths for this test relative to the root directory we're using for
#     all tests (YB_TEST_LOG_ROOT_DIR).
#   is_gtest_test
#     Whether this is a Google Test test ("true" or "false")
#   TEST_TMPDIR (exported)
#     The temporary directory for the test to use. The test should also run in this directory so
#     that the core file would be generated there, if any.
#   test_cmd_line
#     An array representing the test command line to run, including the JUnit-compatible test result
#     XML file location and --gtest_filter=..., if applicable.
#   raw_test_log_path
#     The path to output the raw test log to (before any filtering by stacktrace_addr2line.pl).
#   test_log_path
#     The final log path (after any applicable filtering and appending additional debug info).
#   xml_output_file
#     The location at which JUnit-compatible test result XML file should be generated.
#   test_failed
#     This is being set to "false" initially.
#   test_log_url
#     Test log URL served by Jenkins. This is only set when running on Jenkins.
#
# This function also removes the old XML output file at the path "$xml_output_file".

prepare_for_running_test() {
  expect_num_args 0 "$@"
  expect_vars_to_be_set \
    YB_TEST_LOG_ROOT_DIR \
    test_descriptor
  validate_test_descriptor "$test_descriptor"

  if [[ "$test_descriptor" =~ ::: ]]; then
    rel_test_binary=${test_descriptor%:::*}
    test_name=${test_descriptor#*:::}
    run_at_once=false
    what_test_str="test $test_name"
  else
    # Run all test cases in the given test binary
    rel_test_binary=$test_descriptor
    test_name=""
    run_at_once=true
    what_test_str="all tests"
  fi

  local test_binary_sanitized=$( sanitize_for_path "$rel_test_binary" )
  local test_name_sanitized=$( sanitize_for_path "$test_name" )

  # If there are ephermeral drives, pick a random one for this test and create a symlink from
  #
  # $BUILD_ROOT/yb-test-logs/$test_binary_sanitized
  # ^^^^^^^^^^^^^^^^^^^^^^^^
  # ($YB_TEST_LOG_ROOT_DIR)
  #
  # to /mnt/<drive>/<some_unique_path>/test-workspace/<testname>.
  # Otherwise, simply create the directory under $BUILD_ROOT/yb-test-logs.
  test_dir="$YB_TEST_LOG_ROOT_DIR/$test_binary_sanitized"
  if [[ ! -d $test_dir ]]; then
    create_dir_on_ephemeral_drive "$test_dir" "test-workspace/$test_binary_sanitized"
  fi

  rel_test_log_path_prefix="$test_binary_sanitized"
  if "$run_at_once"; then
    # Make this similar to the case when we run tests separately. Pretend that the test binary name
    # is the test name.
    rel_test_log_path_prefix+="/${rel_test_binary##*/}"
  else
    rel_test_log_path_prefix+="/$test_name_sanitized"
  fi
  # At this point $rel_test_log_path contains the common prefix of the log (.log), the temporary
  # directory (.tmp), and the XML report (.xml) for this test relative to the yb-test-logs
  # directory.

  local abs_test_binary_path=$( create_abs_test_binary_path "$rel_test_binary" )
  test_cmd_line=( "$abs_test_binary_path" )

  test_log_path_prefix="$YB_TEST_LOG_ROOT_DIR/$rel_test_log_path_prefix"
  xml_output_file="$test_log_path_prefix.xml"
  if is_known_non_gtest_test_by_rel_path "$rel_test_binary"; then
    is_gtest_test=false
  else
    test_cmd_line+=( "--gtest_output=xml:$xml_output_file" )
    is_gtest_test=true
  fi

  if ! "$run_at_once" && "$is_gtest_test"; then
    test_cmd_line+=( "--gtest_filter=$test_name" )
  fi

  export TEST_TMPDIR="$test_log_path_prefix.tmp"
  mkdir_safe "$TEST_TMPDIR"
  test_log_path="$test_log_path_prefix.log"
  raw_test_log_path="${test_log_path_prefix}__raw.log"

  # gtest won't overwrite old junit test files, resulting in a build failure
  # even when retries are successful.
  rm -f "$xml_output_file"

  test_failed=false

  if [[ -n ${BUILD_URL:-} ]]; then
    if [[ -z $test_log_url_prefix ]]; then
      echo "Expected test_log_url_prefix to be set when running on Jenkins." >&2
      exit 1
    fi
    test_log_url="$test_log_url_prefix/$rel_test_log_path_prefix.log"
  fi
}

# Looks for a core dump file in the specified directory, shows a stack trace using gdb, and removes
# the core dump file to save space.
# Inputs:
#   rel_test_binary
#     Test binary path relative to $BUILD_ROOT
#   core_dir
#     The directory to look for a core dump file in.
#   test_log_path
#     If this is defined and is not empty, any output generated by this function is appended to the
#     file at this path.
process_core_file() {
  if [[ $# -ne 0 ]]; then
    echo "$FUNCNAME does not take any arguments" >&2
    exit 1
  fi
  expect_vars_to_be_set \
    core_dir \
    rel_test_binary
  local abs_test_binary_path=$BUILD_ROOT/$rel_test_binary

  local append_output_to=/dev/null
  if [[ -n ${test_log_path:-} ]]; then
    append_output_to=$test_log_path
  fi

  if [[ ! -d $core_dir ]]; then
    echo "$FUNCNAME: directory '$core_dir' does not exist" >&2
    exit 1
  fi
  local core_path=$core_dir/core
  if [[ -f $core_path ]]; then
    ( echo "Found a core file at '$core_path', backtrace:" | tee -a "$append_output_to" ) >&2
    local core_binary_path=$abs_test_binary_path
    # The core might have been generated by yb-master or yb-tserver launched as part of an
    # ExternalMiniCluster.
    # TODO(mbautin): don't run gdb the second time in case we get the binary name right the first
    #                time around.
    local gdb_output=$(
      "${GDB_CORE_BACKTRACE_CMD_PREFIX[@]}" "$abs_test_binary_path" "$core_path" 2>&1
    )
    if [[ "$gdb_output" =~ Core\ was\ generated\ by\ .(yb-master|yb-tserver) ]]; then
      # The test program may be launching masters and tablet servers through an ExternalMiniCluster.
      core_binary_path="$BUILD_ROOT/bin/${BASH_REMATCH[1]}"
    fi

    set +e
    (
      set -x
      "${GDB_CORE_BACKTRACE_CMD_PREFIX[@]}" "$core_binary_path" "$core_path" 2>&1 | \
        egrep -v "^\[New LWP [0-9]+\]$" | tee -a "$append_output_to"
    ) >&2
    set -e
    echo
    rm -f "$core_path"  # to save disk space
    echo >&2
  fi
}

# Checks if the there is no XML file at path "$xml_output_file". This may happen if a gtest test
# suite fails before it has a chance to generate an XML output file. In that case, this function
# generates a substitute XML output file using parse_test_failure.py, but only if the log file
# exists at its expected location.
handle_xml_output() {
  expect_vars_to_be_set \
    rel_test_binary \
    test_log_path \
    xml_output_file

  if [[ ! -f "$xml_output_file" ]]; then
    if is_known_non_gtest_test_by_rel_path "$rel_test_binary"; then
      echo "$rel_test_binary is a known non-gtest binary, OK that it did not produce XML" \
           "output" >&2
    else
      echo "$rel_test_binary failed to produce an XML output file at $xml_output_file" >&2
      test_failed=true
    fi
    if [[ -f "$test_log_path" ]]; then
      echo "Generating an XML output file using parse_test_failure.py: $xml_output_file" >&2
      "$YB_SRC_ROOT"/build-support/parse_test_failure.py -x <"$test_log_path" >"$xml_output_file"
    else
      echo "Test log path '$test_log_path' does not exist, cannot generate missing XML output" \
           "file '$xml_output_file'" >&2
      test_failed=true
    fi
  fi

  if [[ -n ${BUILD_URL:-} ]]; then
    if "$test_failed"; then
      echo "Updating $xml_output_file with a link to test log" >&2
    fi
    "$YB_SRC_ROOT"/build-support/update_test_result_xml.py \
      --result-xml "$xml_output_file" \
      --log-url "$test_log_url"
  fi
}

postprocess_test_log() {
  expect_num_args 0 "$@"
  expect_vars_to_be_set \
    STACK_TRACE_FILTER \
    abs_test_binary_path \
    raw_test_log_path \
    test_log_path \
    test_log_path_prefix

  if [[ ! -f $raw_test_log_path ]]; then
    # We must have failed even before we could run the test.
    return
  fi

  local stack_trace_filter_err_path="${test_log_path_prefix}__stack_trace_filter_err.txt"

  if [[ $STACK_TRACE_FILTER == "cat" ]]; then
    # Don't pass the binary name as an argument to the cat command.
    set +e
    "$STACK_TRACE_FILTER" <"$raw_test_log_path" 2>"$stack_trace_filter_err_path" >"$test_log_path"
  else
    set +ex
    "$STACK_TRACE_FILTER" "$abs_test_binary_path" <"$raw_test_log_path" \
      >"$test_log_path" 2>"$stack_trace_filter_err_path"
  fi
  local stack_trace_filter_exit_code=$?
  set -e

  if [[ $stack_trace_filter_exit_code -ne 0 ]]; then
    # Stack trace filtering failed, create an output file with the error message.
    (
      echo "Failed to run the stack trace filtering program '$STACK_TRACE_FILTER'"
      echo
      echo "Standard error from '$STACK_TRACE_FILTER'":
      cat "$stack_trace_filter_err_path"
      echo

      echo "Raw output:"
      echo
      cat "$raw_test_log_path"
    ) >"$test_log_path"
    test_failed=true
  fi
  rm -f "$raw_test_log_path" "$stack_trace_filter_err_path"
}

# Sets test log URL prefix if we're running in Jenkins.
set_test_log_url_prefix() {
  test_log_url_prefix=""
  local build_dir_name=${BUILD_ROOT##*/}
  if [[ -n ${BUILD_URL:-} ]]; then
    build_url_no_trailing_slash=${BUILD_URL%/}
    test_log_url_prefix="${build_url_no_trailing_slash}/artifact/build/$build_dir_name/yb-test-logs"
  fi
}

run_one_test() {
  expect_num_args 0 "$@"
  expect_vars_to_be_set \
    BUILD_ROOT \
    TEST_TMPDIR \
    test_cmd_line \
    test_failed \
    rel_test_binary

  # We expect the exact string "false" here for added safety.
  if [[ $test_failed != "false" ]]; then
    fatal "Expected test_failed to be false before running test, found: $test_failed"
  fi

  local -i timeout_sec
  if [[ -n ${YB_TEST_TIMEOUT:-} ]]; then
    timeout_sec=$YB_TEST_TIMEOUT
  else
    if [[ $rel_test_binary == "rocksdb-build/compact_on_deletion_collector_test" ]]; then
      # This test is particularly slow on TSAN, and it has to be run all at once (we cannot use
      # --gtest_filter) because of dependencies between tests.
      timeout_sec=$INCREASED_TEST_TIMEOUT_SEC
    else
      timeout_sec=$DEFAULT_TEST_TIMEOUT_SEC
    fi
  fi

  pushd "$TEST_TMPDIR" >/dev/null
  set +e
  (
    # This may fail with an error message, and that's what we want. We will still run the test.
    # In case we do manage to set the core file size limit, we will restore the previous limit after
    # we exit the subshell.
    ulimit -c unlimited
    "$BUILD_ROOT"/bin/run-with-timeout $(( $timeout_sec + 1 )) "${test_cmd_line[@]}" \
      &>"$test_log_path"
  )
  test_exit_code=$?
  set -e

  popd >/dev/null

  if [[ $test_exit_code -ne 0 ]]; then
    test_failed=true
  fi
}

handle_test_failure() {
  expect_num_args 0 "$@"
  expect_vars_to_be_set \
    TEST_TMPDIR \
    global_exit_code \
    rel_test_log_path_prefix \
    test_exit_code \
    test_cmd_line \
    test_log_path

  if ! "$test_failed" & ! did_test_succeed "$test_exit_code" "$test_log_path"; then
    test_failed=true
  fi

  if "$test_failed"; then
    (
      echo
      echo "TEST FAILURE"
      echo "Test command: ${test_cmd_line[@]}"
      echo "Test exit status: $test_exit_code"
      echo "Log path: $test_log_path"
      if [[ -n ${BUILD_URL:-} ]]; then
        if [[ -z ${test_log_url_prefix:-} ]]; then
          fatal "Expected test_log_url_prefix to be set if BUILD_URL is defined"
        fi
        # Produce a URL like
        # https://jenkins.dev.yugabyte.com/job/yugabyte-with-custom-test-script/47/artifact/build/debug/yb-test-logs/bin__raft_consensus-itest/RaftConsensusITest_TestChurnyElections.log
        echo "Log URL: $test_log_url_prefix/$rel_test_log_path_prefix.log"
      fi
      if [[ -f $test_log_path ]]; then
        if egrep -q "$RELEVANT_LOG_LINES_RE" "$test_log_path"; then
          echo "Relevant log lines:"
          egrep "$RELEVANT_LOG_LINES_RE" "$test_log_path"
        fi
      fi
    ) >&2
    core_dir=$TEST_TMPDIR
    process_core_file
    unset core_dir
    global_exit_code=1
  fi
}

delete_successful_output_if_needed() {
  expect_vars_to_be_set \
    TEST_TMPDIR \
    test_log_path
  # Mac OS X slaves are having trouble uploading artifacts, so we delete logs and temporary
  # files for successful tests.
  if is_mac && ! "$test_failed"; then
    # This will be shown on console output if the test fails for whatever reason after this point
    # (that would indicate a bug in test scripts).
    echo "Deleting '$test_log_path' after a successful test on Mac OS X" >&2
    rm -rf "$test_log_path" "$TEST_TMPDIR"
  fi
}

run_test_and_process_results() {
  run_one_test
  postprocess_test_log
  handle_test_failure
  handle_xml_output
  delete_successful_output_if_needed
}

set_asan_tsan_options() {
  expect_vars_to_be_set BUILD_ROOT

  local -r build_root_basename=${BUILD_ROOT##*/}

  # We don't add a hyphen in the end of the following regex, because there is a "tsan_slow" build
  # type.
  if [[ $build_root_basename =~ ^(asan|tsan) ]]; then
    # Suppressions require symbolization. We'll default to using the symbolizer in thirdparty.
    # If ASAN_SYMBOLIZER_PATH is already set but that file does not exist, we'll report that and
    # still use the default way to find the symbolizer.
    if [[ -z ${ASAN_SYMBOLIZER_PATH:-} || ! -f ${ASAN_SYMBOLIZER_PATH:-} ]]; then
      if [[ -n "${ASAN_SYMBOLIZER_PATH:-}" ]]; then
        log "ASAN_SYMBOLIZER_PATH is set to '$ASAN_SYMBOLIZER_PATH' but that file does not " \
            "exist, reverting to default behavior."
      fi

      for asan_symbolizer_candidate_path in \
          "$YB_THIRDPARTY_DIR/installed/bin/llvm-symbolizer" \
          "$YB_THIRDPARTY_DIR/clang-toolchain/bin/llvm-symbolizer"; do
        if [[ -f "$asan_symbolizer_candidate_path" ]]; then
          ASAN_SYMBOLIZER_PATH=$asan_symbolizer_candidate_path
          break
        fi
      done
    fi

    if [[ ! -f $ASAN_SYMBOLIZER_PATH ]]; then
      log "ASAN symbolizer at '$ASAN_SYMBOLIZER_PATH' still does not exist."
      ( set -x; ls -l "$ASAN_SYMBOLIZER_PATH" )
    elif [[ ! -x $ASAN_SYMBOLIZER_PATH ]]; then
      log "ASAN symbolizer at '$ASAN_SYMBOLIZER_PATH' is not binary, updating permissions."
      ( set -x; chmod a+x "$ASAN_SYMBOLIZER_PATH" )
    fi

    export ASAN_SYMBOLIZER_PATH
  fi

  if [[ $build_root_basename =~ ^asan- ]]; then
    # Enable leak detection even under LLVM 3.4, where it was disabled by default.
    # This flag only takes effect when running an ASAN build.
    ASAN_OPTIONS="${ASAN_OPTIONS:-} detect_leaks=1"
    export ASAN_OPTIONS

    # Set up suppressions for LeakSanitizer
    LSAN_OPTIONS="${LSAN_OPTIONS:-} suppressions=$YB_SRC_ROOT/build-support/lsan-suppressions.txt"

    # If we print out object addresses somewhere else, we can match them to LSAN-reported
    # addresses of leaked objects.
    LSAN_OPTIONS+=" report_objects=1"
    export LSAN_OPTIONS
  fi

  # Don't add a hyphen after the regex so we can handle both tsan and tsan_slow.
  if [[ $build_root_basename =~ ^tsan ]]; then
    # Configure TSAN (ignored if this isn't a TSAN build).
    #
    # Deadlock detection (new in clang 3.5) is disabled because:
    # 1. The clang 3.5 deadlock detector crashes in some YB unit tests. It
    #    needs compiler-rt commits c4c3dfd, 9a8efe3, and possibly others.
    # 2. Many unit tests report lock-order-inversion warnings; they should be
    #    fixed before reenabling the detector.
    TSAN_OPTIONS="${TSAN_OPTIONS:-} detect_deadlocks=0"
    TSAN_OPTIONS="$TSAN_OPTIONS suppressions=$YB_SRC_ROOT/build-support/tsan-suppressions.txt"
    TSAN_OPTIONS="$TSAN_OPTIONS history_size=7"
    TSAN_OPTIONS="$TSAN_OPTIONS external_symbolizer_path=$ASAN_SYMBOLIZER_PATH"
    export TSAN_OPTIONS
  fi
}

did_test_succeed() {
  expect_num_args 2 "$@"
  local -i -r exit_code=$1
  local -r log_path=$2
  if [[ $exit_code -ne 0 ]]; then
    return 1  # "false" value in bash, meaning the test failed
  fi

  if [[ ! -f "$log_path" ]]; then
    log "Log path '$log_path' not found."
    return 1
  fi

  if grep -q 'Running 0 tests from 0 test cases' "$log_path" && \
     ! egrep -q 'YOU HAVE [[:digit:]]+ DISABLED TEST' "$log_path"; then
    log 'No tests were run, and no disabled tests found, invalid test filter?'
    return 1
  fi

  if grep -q 'LeakSanitizer: detected memory leaks' "$log_path"; then
    log 'Detected memory leaks'
    return 1
  fi

  if grep -q 'AddressSanitizer: heap-use-after-free' "$log_path"; then
    log 'Detected use of freed memory'
    return 1
  fi

  if grep -q 'AddressSanitizer: undefined-behavior' "$log_path"; then
    log 'Detected undefined behavior'
    return 1
  fi

  if grep -q 'ThreadSanitizer' "$log_path"; then
    log 'ThreadSanitizer failures'
    return 1
  fi

  if egrep -q 'Leak check.*detected leaks' "$log_path"; then
    log 'Leak check failures'
    return 1
  fi

  if egrep -q 'Segmentation fault: ' "$log_path"; then
    log 'Segmentation fault'
    return 1
  fi

  # When signals show up in the test log, that's usually not good. We can see how many of these we
  # get and gradually prune false positives.
  local signal_str
  for signal_str in \
      SIGHUP \
      SIGINT \
      SIGQUIT \
      SIGILL \
      SIGTRAP \
      SIGIOT \
      SIGBUS \
      SIGFPE \
      SIGKILL \
      SIGUSR1 \
      SIGSEGV \
      SIGUSR2 \
      SIGPIPE \
      SIGALRM \
      SIGTERM \
      SIGSTKFLT \
      SIGCHLD \
      SIGCONT \
      SIGSTOP \
      SIGTSTP \
      SIGTTIN \
      SIGTTOU \
      SIGURG \
      SIGXCPU \
      SIGXFSZ \
      SIGVTALRM \
      SIGPROF \
      SIGWINCH \
      SIGIO \
      SIGPWR; do
    if grep -q " $signal_str " "$log_path"; then
      log "Caught signal: $signal_str"
      return 1
    fi
  done

  if grep -q 'Check failed: ' "$log_path"; then
    log 'Check failed'
    return 1
  fi

  return 0
}

find_test_binary() {
  expect_num_args 1 "$@"
  local binary_name=$1
  if [[ $binary_name == "client_samples-test" ]]; then
    binary_name+=".sh"
  fi
  expect_vars_to_be_set BUILD_ROOT
  local dirs_tried=""
  local rel_dir
  for rel_dir in "${VALID_TEST_BINARY_DIRS[@]}"; do
    local binary_dir=$BUILD_ROOT/$rel_dir
    local candidate=$binary_dir/$binary_name
    if [[ -f "$candidate" ]]; then
      echo "$candidate"
      return
    fi
    if [[ -n $dirs_tried ]]; then
      dirs_tried+=", "
    fi
    dirs_tried+="$binary_dir"
  done
  fatal "Could not find binary $binary_name in any of the directories: $dirs_tried"
}

show_disk_usage() {
  header "Disk usage (df -h)"

  df -h

  echo
  horizontal_line
  echo
}

# -------------------------------------------------------------------------------------------------
# Initialization
# -------------------------------------------------------------------------------------------------

if is_mac; then
  # Stack trace address to line number conversion is disabled on Mac OS X as of Apr 2016.
  # See https://yugabyte.atlassian.net/browse/ENG-37
  STACK_TRACE_FILTER=cat
else
  STACK_TRACE_FILTER=$YB_SRC_ROOT/build-support/stacktrace_addr2line.pl
fi

readonly jenkins_job_and_build=${JOB_NAME:-unknown_job}__${BUILD_NUMBER:-unknown_build}
