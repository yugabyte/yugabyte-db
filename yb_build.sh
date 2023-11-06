#!/usr/bin/env bash
#
# Copyright (c) YugaByte, Inc.
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
script_name=${0##*/}
script_name=${script_name%.*}

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}"/build-support/common-test-env.sh

ensure_option_has_arg() {
  if [[ $# -lt 2 ]]; then
    echo "Command line option $1 expects an argument" >&2
    exit 1
  fi
}

show_help() {
  cat >&2 <<-EOT
yb_build.sh (or "ybd") is the main build tool for Yugabyte Database.
Usage: ${0##*/} [<options>] [<build_type>] [<target_keywords>] [<yb_env_var_settings>]
Options:

  --help, -h
    Show help.

Build options:

  --arch <architecture>
    Build for the given architecture. Currently only relevant for Apple Silicon where we can build
    for x86_64 and arm64 (no cross-compilation support yet).

  --compiler-type
    Specify compiler type, e.g. gcc, clang, or a specific version, e.g. gcc10 or clang12.
  --gcc, --gcc<version>, --clang, --clang<version>
    A shorter way to achieve the same thing as --compiler-type.

  -j <parallelism>, -j<parallelism>
    Build using the given number of concurrent jobs (defaults to the number of CPUs).

  --clean
    Remove the build directory for the appropriate build type before building.
  --clean-venv
    Remove and re-create the Python virtual environment before building.
  --clean-all
    Remove all build directories and the Python virtual environment before building.
  --clean-force, --cf, -cf
    A combination of --clean and --force.
  --clean-thirdparty
    Remove previously built third-party dependencies and rebuild them. Implies --clean.
  --force, -f, -y
    Run a clean build without asking for confirmation even if a clean build was recently done.

  --skip-build, --sb
    Skip all kinds of build (C++, Java)
  --skip-java-build, --skip-java, --sjb, --sj, --no-java, --nj
    Do not package and install java source code.
  --skip-cxx-build, --scb
    Skip C++ build. This is useful when repeatedly debugging tests using this tool and not making
    any changes to the code.
  --no-postgres, --skip-postgres, --np, --sp
    Skip PostgreSQL build
  --no-latest-symlink
    Disable the creation/overwriting of the "latest" symlink in the build directory.
  --no-tests, --skip-tests
    Do not build tests
  --no-tcmalloc
    Do not use tcmalloc.
  --use-google-tcmalloc, --google-tcmalloc
    Use Google's implementation of tcmalloc from https://github.com/google/tcmalloc
  --no-google-tcmalloc, --use-gperftools-tcmalloc, --gperftools-tcmalloc
    Use the gperftools implementation of tcmalloc

  --clean-postgres
    Do a clean build of the PostgreSQL subtree.
  --rebuild-postgres
    Clean and rebuild PostgeSQL code

  --java-only, --jo
    Only build Java code
  --resolve-java-dependencies
    Force Maven to download all Java dependencies to the local repository

  --build-yugabyted-ui
    Build yugabyted-ui. If specified with --cmake-only, it won't be built.

  --target, --targets
    Pass the given target or set of targets to make or ninja.
  --rebuild-file <source_file_to_rebuild>
    The .o file corresponding to the given source file will be deleted from the build directory
    before the build.
  --rebuild-file <target_name>
    Combines --target and --rebuild-file. Currently only works if target name matches the object
    file name to be deleted.

  --cmake-only
    Only run CMake, don't run any other build steps.
  --force-run-cmake, --frcm
    Ensure that we explicitly invoke CMake from this script. CMake may still run as a result of
    changes made to CMakeLists.txt files if we just invoke make on the CMake-generated Makefile.
  --force-no-run-cmake, --fnrcm, --skip-cmake, --no-cmake
    The opposite of --force-run-cmake. Makes sure we do not run CMake.
  --cmake-args
    Additional CMake arguments
  --make
    Use the Make backend (as opposed to Ninja).
  --ninja
    Use the Ninja backend instead of Make for CMake. This provides faster build speed in case
    most part of the code is already built.
  --make-ninja-extra-args <extra_args>
    Extra arguments for the build tool such as Unix Make or Ninja.

  --thirdparty-dir <thirdparty_dir>
    Use a third-party directory other than <source_tree_root>/thirdparty. This is useful when using
    multiple build directories with different versions of YB code so we can avoid building
    third-party code multiple times.
  --download-thirdparty, --dltp  (This is the default.)
    Use prebuilt third-party dependencies, downloadable e.g. from a GitHub release. Also records the
    third-party URL in the build root so that further invocations of yb_build.sh don't reqiure
    this option (this could be reset by --clean).
  --no-download-thirdparty|--ndltp)
    Disable downloading pre-built third-party dependencies.
  --no-rebuild-thirdparty, --nbtp, --nb3p, --nrtp, --nr3p
    Skip building third-party libraries, even if the thirdparty directory has changed in git.

  --no-ccache
    Do not use ccache. Useful when debugging build scripts or compiler/linker options.
  --generate-build-debug-scripts, --gen-build-debug-scripts, --gbds
    Specify this to generate one-off shell scripts that could be used to re-run and understand
    failed compilation commands.
  --write-build-descriptor <build_descriptor_path>
    Write a "build descriptor" file. A "build descriptor" is a YAML file that provides information
    about the build root, compiler used, etc.
  --remote
    Prefer a remote build on an auto-scaling cluster of build workers. The parallelism is picked
    automatically based on the current number of build workers.
  --build-root
    The build root directory, e.g. build/debug-gcc-dynamic-enterprise. This is used in scripting
    and is checked against other parameters.
  --show-compiler-cmd-line, --sccl
    Show compiler command line.
  --export-compile-commands, --ccmds
    Export the C/C++ compilation database. Equivalent to setting YB_EXPORT_COMPILE_COMMANDS to 1.
  --export-compile-commands-cxx-only, --ccmdscxx
    Only export the compilation commands for C++ code. Compilation database generation for Postgres
    C code can be time-consuming and this
  --linuxbrew or --no-linuxbrew
    Specify in order to do a Linuxbrew based build, or specifically prohibit doing so. This
    influences the choice of prebuilt third-party archive. This can also be specified using the
    YB_USE_LINUXBREW environment variable.
  --static-analyzer
    Enable Clang static analyzer
  --clangd-index
    Build a static Clangd index using clangd-indexer.
  --clangd-index-only, --cio
    A combination of --clangd-index, --skip-initdb, and --skip-java
  --clangd-index-format <format>
    Clangd index format ("binary" or "yaml"). A YAML index can be moved to another directory.
  --mvn-opts <maven_options>
    Specify additional Maven options for Java build/tests.
  --lto <lto_type>, --thin-lto, --full-lto, --no-lto
    LTO (link time optimization) type, e.g. "thin" (faster to link) or "full" (faster code; see
    https://llvm.org/docs/LinkTimeOptimization.html and https://clang.llvm.org/docs/ThinLTO.html).
    Can also be specified by setting environment variable YB_LINKING_TYPE to thin-lto or full-lto.
    Set YB_LINKING_TYPE to 'dynamic' to disable LTO.
  --no-initdb, --skip-initdb
    Skip the initdb step. The initdb build step is mostly single-threaded and can be executed on a
    low-CPU build machine even if the majority of the build is being executed on a high-CPU host.
  --skip-test-log-rewrite
    Skip rewriting the test log.
  --skip-final-lto-link
    For LTO builds, skip the final linking step for server executables, which could take many
    minutes.
  --cxx-test-filter-re, --cxx-test-filter-regex
    Regular expression for filtering C++ tests to build. This regular expression is not anchored
    on either end, so e.g. you can specify a substring of the test name. Use ^ or $ as needed.

Linting options:

  --shellcheck
    Check various Bash scripts in the codebase.
  --java-lint
    Run a simple shell-based "linter" on our Java code that verifies that we are importing the right
    methods for assertions and using the right test runners. We exit the script after this step.

Test options:

  --ctest
    Runs ctest in the build directory after the build. This is mutually exclusive with --cxx-test.
    This will also skip building Java code, unless --run-java-tests is specified.
  --ctest-args
    Specifies additional arguments to ctest. Implies --ctest.

  --cxx-test <cxx_test_program_name>
    Build and run the given C++ test program. We run the test using ctest. Specific tests within the
    test program can be chosen using --gtest_filter.
  --gtest_filter
    Use the given filter to select Google Test tests to run. Uses with --cxx-test.
  --test-args
    Extra arguments to pass to the test. Used with --cxx-test.

  --sanitizer-extra-options, --extra-sanitizer-options
    Extra options to pass to ASAN/LSAN/UBSAN/TSAN. See https://goo.gl/VbTjHH for possible values.
  --sanitizers-enable-coredump
    When running tests with LLVM sanitizers (ASAN/TSAN/etc.), enable core dump.
  --sanitizer-verbosity
    Use the given verbosity value for clang sanitizers. The default is 0.

  --collect-java-tests
    Collect the set of Java test methods into a file
  --java-tests, --run-java-tests
    Run the Java unit tests.
  --java-test <java_test_name>
    Build and run the given Java test. Test name format is e.g.
    org.yb.loadtester.TestRF1Cluster[#testRF1toRF3].
  --run-java-test-methods-separately, --rjtms
    Run each Java test (test method or a parameterized instantiation of a test method) separately
    as its own top-level Maven invocation, writing output to a separate file. This is the default
    when --java-test specifies a particular test method using the [package.]ClassName#testMethod
    syntax, and only makes a difference when running the entire test suite.

  --java-test-args
    Extra arguments to pass to Maven when running tests. Used with --java-test.

  --python-tests
    Run various Python tests (doctest, unit test) and exit.

  --{no,skip}-{test-existence-check,check-test-existence}
    Don't check that all test binaries referenced by CMakeLists.txt files exist.
  --num-repetitions, --num-reps, -n
    Repeat a C++/Java test this number of times. This delegates to the repeat_unit_test.sh script.
  --test-parallelism, --tp N
    When running tests repeatedly, run up to N instances of the test in parallel. Equivalent to the
    --parallelism argument of repeat_unit_test.sh.
  --host-for-tests
    Use this host for running tests. Could also be set using the YB_HOST_FOR_RUNNING_TESTS env
    variable.
  --test-timeout-sec
    Test timeout in seconds
  --remove-successful-test-logs
    Remove logs after a successful test run.
  --stop-at-failure, --stop-on-failure
    Stop running further iterations after the first failure happens when running a unit test
    repeatedly.
  --stack-trace-error-status, --stes
    When running tests, print stack traces when error statuses are generated. Only works in
    non-release mode.
  --stack-trace-error-status-re, --stesr
    When running tests, print stack traces when error statuses matching the given regex are
    generated. Only works in non-release mode.
  --cmake-unit-tests
    Run our unit tests for CMake code. This should be much faster than running the build.
  --
    Pass all arguments after -- to repeat_unit_test.

  --extra-daemon-flags, --extra-daemon-args <extra_daemon_flags>
    Extra flags to pass to mini-cluster daemons (master/tserver). Note that bash-style quoting won't
    work here -- they are naively split on spaces.
  --(with|no)-fuzz-targets
    Build|Do not build fuzz targets. By default - do not build.
  --(with|no)-odyssey
    Specify whether to build Odyssey (PostgreSQL connection pooler). Not building by default.
  --enable-ysql-conn-mgr-test
    Use YSQL Connection Manager as an endpoint when running unit tests. Could also be set using
    the YB_ENABLE_YSQL_CONN_MGR_IN_TESTS env variable.

  --validate-args-only
    Only validate command-line arguments and exit immediately. Suppress all unnecessary output.

Debug options:

  --verbose
    Show debug output
  --bash-debug
    Show detailed debug information for each command executed by this script.
  --super-bash-debug
    Log the location of every command executed in this script

Build types:
  ${VALID_BUILD_TYPES[*]}
  (default: debug)

Supported target keywords:
  ...-test           - build and run a C++ test
  [yb-]master        - master executable
  [yb-]tserver       - tablet server executable
  daemons            - yb-master, yb-tserver, and the postgres server
  packaged[-targets] - targets that are required for a release package
  initdb             - Initialize the initial system catalog snapshot for fast cluster startup
  reinitdb           - Reinitialize the initial system catalog snapshot for fast cluster startup

Setting YB_... environment variables on the command line:
  YB_SOME_VARIABLE1=some_value1 YB_SOME_VARIABLE2=some_value2
The same also works for postgres_FLAGS_... variables.

---------------------------------------------------------------------------------------------------

EOT
}

set_cxx_test_name() {
  expect_num_args 1 "$@"
  if [[ $cxx_test_name == "$1" ]]; then
    # Duplicate test name specified, ignore.
    return
  fi
  if [[ -n $cxx_test_name ]]; then
    fatal "Only one C++ test name can be specified (found '$cxx_test_name' and '$1')."
  fi
  if [[ $1 == TEST* ]]; then
    local test_source_path
    test_source_path=$(
      ( cd "$YB_SRC_ROOT/src" && git grep "$1" ) | cut -d: -f1 | sort | uniq
    )
    if [[ ! -f "$YB_SRC_ROOT/src/$test_source_path" ]]; then
      fatal "Failed to identify test path based on code substring $cxx_test_name." \
            "Grep result: $test_source_path"
    fi
    cxx_test_name=${test_source_path##*/}
    cxx_test_name=${cxx_test_name%.cc}

    # A convenience syntax for copying and pasting a line from a C++ test.
    # E.g. --cxx-test='TEST(FormatTest, Time) {' or even
    # --cxx-test='TEST_F_EX(ClientTest, CompactionStatusWaitingForHeartbeats, CompactionClientTest)'

    local gtest_filter
    local identifier='([a-zA-Z_][a-zA-Z_0-9]*)'
    if [[ $1 =~ ^(TEST_F_EX)\($identifier,\ *$identifier,\ *$identifier\) ||
          $1 =~ ^(TEST|TEST_F)\($identifier,\ *$identifier\) ]]; then
      gtest_filter=${BASH_REMATCH[2]}.${BASH_REMATCH[3]}
    elif [[ $1 =~ ^TEST_P\($identifier,\ *$identifier\) ]]; then
      # Create a filter with wildcards that match all possiblilities.  For example,
      # - PackingVersion/PgPackedRowTest.AddDropColumn/kV1
      # - PackingVersion/PgPackedRowTest.AddDropColumn/kV2
      gtest_filter="*/${BASH_REMATCH[1]}.${BASH_REMATCH[2]}/*"
    else
      fatal "Could not determine gtest test filter from source substring $1"
    fi
    export YB_GTEST_FILTER=$gtest_filter

    log "Determined C++ test based on source substring:" \
        "--cxx-test=$cxx_test_name --gtest_filter=$gtest_filter"
  else
    cxx_test_name=$1
  fi
  running_any_tests=true
  build_java=false
}

set_java_test_name() {
  expect_num_args 1 "$@"
  if [[ $java_test_name == "$1" ]]; then
    # Duplicate test name specified, ignore.
    return
  fi
  if [[ -n $java_test_name ]]; then
    fatal "Only one Java test name can be specified (found '$java_test_name' and '$1')."
  fi
  running_any_tests=true
  run_java_tests=true
  build_java=true
  java_test_name=$1
}

set_vars_for_cxx_test() {
  if [[ -n $cxx_test_name ]]; then
    make_targets+=( "$cxx_test_name" )
  fi

  # This is necessary to avoid failures if we are just building one test.
  test_existence_check=false
}

print_report_line() {
  local format_suffix=$1
  shift
  printf '%-32s : '"$format_suffix\n" "$@"
}

# Report the time taken for a particular operation, based on the start and end time variables.
# If these variables are not set, then no report line is printed.
report_time() {
  expect_num_args 2 "$@"
  local description=$1
  local time_var_prefix=$2

  local start_time_var_name="${time_var_prefix}_start_time_sec"
  local end_time_var_name="${time_var_prefix}_end_time_sec"

  local -i start_time=${!start_time_var_name:-0}
  local -i end_time=${!end_time_var_name:-0}

  if [[ $start_time -ne 0 && $end_time -ne 0 ]]; then
    local caption="$description time"
    print_report_line "%d seconds" "$caption" "$(( end_time - start_time ))"
  fi
}

print_report() {
  if [[ ${show_report} == "true" ]]; then
    (
      echo
      thick_horizontal_line
      echo "YUGABYTE BUILD SUMMARY"
      thick_horizontal_line
      print_report_line "%s" "Build type" "${build_type:-undefined}"
      if [[ -n ${YB_COMPILER_TYPE:-} ]]; then
        print_report_line "%s" "C/C++ compiler" "$YB_COMPILER_TYPE"
      fi
      print_report_line "%s" "Build architecture" "${YB_TARGET_ARCH}"
      print_report_line "%s" "Build directory" "${BUILD_ROOT:-undefined}"
      print_report_line "%s" "Third-party dir" "${YB_THIRDPARTY_DIR:-undefined}"
      if using_linuxbrew; then
        print_report_line "%s" "Linuxbrew dir" "${YB_LINUXBREW_DIR:-undefined}"
      fi

      set +u
      local make_targets_str="${make_targets[*]}"
      set -u
      if [[ -n $make_targets_str ]]; then
        print_report_line "%s" "Targets" "$make_targets_str"
      fi
      report_time "CMake"                               cmake
      report_time "C++ compilation"                     make
      report_time "Java compilation"                    java_build
      report_time "C++ (one test program)"              cxx_test
      report_time "ctest (multiple C++ test programs)"  ctest
      report_time "Collecting Java tests"               collect_java_tests
      report_time "Java tests"                          java_tests
      report_time "Remote tests"                        remote_tests

      if [[ ${YB_SKIP_BUILD:-} == "1" ]]; then
        echo
        echo "NO COMPILATION WAS DONE AS PART OF THIS BUILD (--skip-build)"
        echo
      fi

      if [[ -n ${YB_BUILD_EXIT_CODE:-} && $YB_BUILD_EXIT_CODE -ne 0 ]]; then
        print_report_line "%s" "Exit code" "$YB_BUILD_EXIT_CODE"
      fi
      horizontal_line
    ) >&2
  fi
}

set_flags_to_skip_build() {
  build_cxx=false
  build_java=false
}

create_build_descriptor_file() {
  if [[ -n $build_descriptor_path ]]; then
    # The format of this file is YAML.

    cat >"$build_descriptor_path" <<-EOT
build_type: "$build_type"
build_arch: "$(uname -m)"
cmake_build_type: "${cmake_build_type:-undefined}"
build_root: "$BUILD_ROOT"
compiler_type: "$YB_COMPILER_TYPE"
thirdparty_dir: "${YB_THIRDPARTY_DIR:-$YB_SRC_ROOT/thirdparty}"
EOT
    if using_linuxbrew; then
      echo "linuxbrew_dir: \"${YB_LINUXBREW_DIR:-}\"" >>"$build_descriptor_path"
    fi
    log "Created a build descriptor file at '$build_descriptor_path'"
  fi
}

create_build_root_file() {
  if [[ -n ${BUILD_ROOT:-} ]]; then
    local latest_build_root_path=$YB_SRC_ROOT/build/latest_build_root
    echo "Saving BUILD_ROOT to $latest_build_root_path"
    echo "$BUILD_ROOT" > "$latest_build_root_path"
  fi
}

capture_sec_timestamp() {
  expect_num_args 1 "$@"
  local current_timestamp
  current_timestamp=$(date +%s)
  eval "${1}_time_sec=$current_timestamp"
}

run_yugabyted_ui_build() {
  # This is a standalone build script.  It honors BUILD_ROOT from the env
  "${YB_SRC_ROOT}/yugabyted-ui/build.sh"
}

run_cxx_build() {
  expect_vars_to_be_set make_file

  # shellcheck disable=SC2154
  if [[ (
          ${force_run_cmake} == "true" ||
          ${cmake_only} == "true" ||
          ! -f ${make_file}
        ) && ${force_no_run_cmake} == "false" ]]
  then
    local cmake_binary
    if is_mac && [[ "${YB_TARGET_ARCH:-}" == "arm64" ]]; then
      cmake_binary=/opt/homebrew/bin/cmake
    else
      if ! which cmake &> /dev/null; then
        echo "Error: cmake not found in PATH" >&2
        exit 1
      fi
      cmake_binary=$( which cmake )
    fi
    log "Using cmake binary: $cmake_binary"
    find "${BUILD_ROOT}" -name "CMake*.log" -exec rm -f {} \;
    log "Running cmake in $PWD"
    capture_sec_timestamp "cmake_start"
    local cmake_stdout_path=${BUILD_ROOT}/cmake_stdout.txt
    local cmake_stderr_path=${BUILD_ROOT}/cmake_stderr.txt
    set +e
    (
      # Always disable remote build (running the compiler on a remote worker node) when running the
      # CMake step.
      #
      # We are modifying YB_REMOTE_COMPILATION inside a subshell on purpose.
      # shellcheck disable=SC2030
      export YB_REMOTE_COMPILATION=0

      set -x
      # We are not double-quoting $cmake_extra_args on purpose to allow multiple arguments.
      # shellcheck disable=SC2086
      "${cmake_binary}" "${cmake_opts[@]}" $cmake_extra_args "${YB_SRC_ROOT}" \
        >"${cmake_stdout_path}" 2>"${cmake_stderr_path}"
    )
    local cmake_exit_code=$?
    set -e
    capture_sec_timestamp "cmake_end"

    # Show the contents of special CMake output files before we show CMake output itself.
    if [[ ${cmake_exit_code} != 0 ]]; then
      log "CMake failed with exit code ${cmake_exit_code}."
      (
        find "${BUILD_ROOT}" -name "CMake*.log" | while read -r cmake_log_path; do
          echo
          echo "--------------------------------------------------------------------------------"
          echo "Contents of ${cmake_log_path}:"
          echo "--------------------------------------------------------------------------------"
          echo
          cat "${cmake_log_path}"
          echo
          echo "--------------------------------------------------------------------------------"
          echo
        done
      ) >&2
    fi

    if [[ -s ${cmake_stdout_path} ]]; then
      if [[ ${cmake_exit_code} != 0 ]]; then
        # Only mark CMake standard output as such in case of an error. Otherwise, just pass it
        # through.
        echo "CMake standard output (also saved to ${cmake_stdout_path}):"
        echo
      fi
      cat "${cmake_stdout_path}"
      if [[ ${cmake_exit_code} != 0 ]]; then
        echo
      fi
    fi
    if [[ -s ${cmake_stderr_path} ]]; then
      echo "CMake standard error (also saved to ${cmake_stderr_path}):" >&2
      echo >&2
      cat "${cmake_stderr_path}" >&2
      echo >&2
    fi

    if [[ ${cmake_exit_code} != 0 ]]; then
      fatal "CMake failed with exit code ${cmake_exit_code}. See additional logging above."
    fi
  fi

  if [[ ${cmake_only} == "true" ]]; then
    log "CMake has been invoked, stopping here (--cmake-only specified)."
    exit
  fi

  if [[ ${build_cxx} == "false" ]]; then
    log "Skipping C++ build after invoking CMake."
    return
  fi

  if [[ ${#object_files_to_delete[@]} -gt 0 ]]; then
    log_empty_line
    log "Deleting object files corresponding to: ${object_files_to_delete[*]}"
    # TODO: can delete multiple files using the same find command.
    for object_file_to_delete in "${object_files_to_delete[@]}"; do
      ( set -x; find "$BUILD_ROOT" -name "$object_file_to_delete" -exec rm -fv {} \; )
    done
    log_empty_line
  fi

  fix_gtest_cxx_test_name
  set_vars_for_cxx_test

  log "Running $make_program in $PWD"
  capture_sec_timestamp "make_start"
  set +u +e  # "set -u" may cause failures on empty lists
  # We are not double-quoting $make_ninja_extra_args on purpose, to allow multiple arguments.
  # shellcheck disable=SC2206
  make_program_args=(
    "-j$YB_MAKE_PARALLELISM"
    "${make_opts[@]}"
    $make_ninja_extra_args
    "${make_targets[@]}"
  )
  set -u
  if [[ ${reduce_log_output} == "true" ]]; then
    time (
      set -x
      "$make_program" "${make_program_args[@]}" | filter_boring_cpp_build_output
    )
  else
    time (
      set -x
      "$make_program" "${make_program_args[@]}"
    )
  fi

  local exit_code=$?
  set -u -e
  capture_sec_timestamp "make_end"

  log "C++ build finished with exit code $exit_code" \
      "(build type: $build_type, compiler: $YB_COMPILER_TYPE)." \
      "Timing information is available above."
  if [[ $exit_code -ne 0 ]]; then
    exit "$exit_code"
  fi

  # Don't check for test binary existence in case targets are explicitly specified.
  if [[ $test_existence_check == "true" && ${#make_targets[@]} -eq 0 ]]; then
    (
      cd "$BUILD_ROOT"
      log "Checking if all test binaries referenced by CMakeLists.txt files exist."
      if ! check_test_existence; then
        log "Re-running test existence check again with more verbose output"
        # We need to do this because sometimes we get an error with no useful output otherwise.
        # We pass a pattern that includes all lines in the output.
        check_test_existence '.*'
        fatal "Some test binaries referenced in CMakeLists.txt files do not exist," \
              "or failed to check. See detailed output above."
      fi
    )
  fi
}

run_repeat_unit_test() {
  export YB_COMPILER_TYPE
  set +u
  local repeat_unit_test_args=(
    "${repeat_unit_test_inherited_args[@]}"
  )
  set -u
  repeat_unit_test_args+=(
    "$@"
    --build-type "$build_type"
    --num-iter "$num_test_repetitions"
  )
  if [[ -n ${YB_TEST_PARALLELISM:-} ]]; then
    repeat_unit_test_args+=( --parallelism "$YB_TEST_PARALLELISM" )
  fi
  if [[ ${verbose} == "true" ]]; then
    repeat_unit_test_args+=( --verbose )
  fi
  (
    set -x
    "$YB_SRC_ROOT"/bin/repeat_unit_test.sh "${repeat_unit_test_args[@]}"
  )
}

run_ctest() {
  # Not setting YB_CTEST_VERBOSE here because we don't want the output of a potentially large number
  # of tests to go to stdout.
  (
    cd "$BUILD_ROOT"
    set -x
    # Not quoting $ctest_args on purpose.
    # shellcheck disable=SC2086
    ctest -j"$YB_NUM_CPUS" --verbose $ctest_args 2>&1 |
      grep -Ev "^[0-9]+: Test timeout computed to be: "
  )
}

run_tests_remotely() {
  ran_tests_remotely=false
  if [[ $running_any_tests == "false" ]]; then
    # Nothing to run remotely.
    return
  fi
  if [[ -n ${YB_HOST_FOR_RUNNING_TESTS:-} && \
        $YB_HOST_FOR_RUNNING_TESTS != "127.0.0.1" && \
        $YB_HOST_FOR_RUNNING_TESTS != "localhost" && \
        $YB_HOST_FOR_RUNNING_TESTS != "$HOSTNAME" && \
        $YB_HOST_FOR_RUNNING_TESTS != "$HOSTNAME."* ]] ; then
    capture_sec_timestamp "remote_tests_start"
    log "Running tests on host '$YB_HOST_FOR_RUNNING_TESTS' (current host is '$HOSTNAME')"

    # Add extra arguments to the sub-invocation of yb_build.sh. We have to add them before the
    # first "--".
    set +u
    sub_yb_build_args=()
    extra_args=( --skip-build --host-for-tests "localhost" --no-report )

    for arg in "${original_args[@]}"; do
      case $arg in
        --)
          sub_yb_build_args+=( "${extra_args[@]}" "$arg" )
          extra_args=()
        ;;
        --clean|--clean-*)
          # Do not pass these arguments to the child yb_build.sh.
        ;;
        *)
          sub_yb_build_args+=( "$arg" )
      esac
    done
    sub_yb_build_args+=( "${extra_args[@]}" )
    set -u

    log "Running tests on server '$YB_HOST_FOR_RUNNING_TESTS': yb_build.sh ${sub_yb_build_args[*]}"
    run_remote_cmd "$YB_HOST_FOR_RUNNING_TESTS" "$YB_SRC_ROOT/yb_build.sh" \
      "${sub_yb_build_args[@]}"

    capture_sec_timestamp "remote_tests_end"
    ran_tests_remotely=true
  fi
}

run_cxx_test() {
  fix_cxx_test_name

  if [[ $num_test_repetitions -eq 1 ]]; then
    (
      set_sanitizer_runtime_options
      cd "$BUILD_ROOT"

      # The following makes our test framework repeat the test log in stdout in addition writing the
      # log file instead of simply redirecting it to the log file. Combined with the --verbose ctest
      # option, this gives us nice real-time test output, while still taking advantage of correct
      # test flags such (e.g. ASAN/TSAN options and suppression rules) that are set in run-test.sh.
      export YB_CTEST_VERBOSE=1

      # --verbose: enable verbose output from tests.  Test output is normally suppressed and only
      # summary information is displayed.  This option will show all test output.
      # --output-on-failure is unnecessary when --verbose is specified. In fact, adding
      # --output-on-failure will result in duplicate output in case of a failure.
      #
      # In this verbose mode, ctest also adds some number (test number?) with a colon in the
      # beginning of every line of the output. We filter that out.
      set -x
      ctest --verbose -R ^"$cxx_test_name"$ 2>&1 | sed 's/^[0-9][0-9]*: //g'
    )
  else
    run_repeat_unit_test "$build_type" "$test_binary_name"
  fi
}

register_file_to_rebuild() {
  expect_num_args 1 "$@"
  local file_name=${1%.o}
  object_files_to_delete+=(
    "$file_name.o"
    "$file_name.c.o"
    "$file_name.cc.o"
  )
}

# This is used for "initdb" and "reinitdb" target keywords.
set_initdb_target() {
  make_targets+=( "initial_sys_catalog_snapshot" )
  build_java=false
}

disable_initdb() {
  export YB_SKIP_INITIAL_SYS_CATALOG_SNAPSHOT=1
}

cleanup() {
  local YB_BUILD_EXIT_CODE=$?
  print_report
  exit "$YB_BUILD_EXIT_CODE"
}

print_saved_log_path() {
  heading "To view log:"$'\n\n'"less '$log_path'"$'\n\n'\
"Or using symlink:"$'\n\n'"less '$latest_log_symlink_path'"$'\n'
}

set_clean_build() {
  # We use is_clean_build in common-build-env.sh.
  # shellcheck disable=SC2034
  is_clean_build=true
  remove_build_root_before_build=true
}

enable_clangd_index_build() {
  should_build_clangd_index=true
  # Compilation database is required before we can build the Clangd index.
  export YB_EXPORT_COMPILE_COMMANDS=1
}

set_cxx_test_filter_regex() {
  expect_num_args 1 "$@"
  force_run_cmake=true
  cmake_opts+=( "-DYB_TEST_FILTER_RE=$1" )
}

# This function is used to propagate a boolean variable to a CMake variable. For example, if we have
# a variable named build_tests, we will propagate that variable to a CMake parameter named
# YB_BUILD_TESTS, and will replace true with ON and false with OFF. This is useful for variables
# that are used in both build scripts and CMake files.
propagate_bool_var_to_cmake() {
  expect_num_args 1 "$@"
  local var_name=$1
  # var_name could be something build_tests. We will propagate that variable to a CMake parameter
  # named e.g. YB_BUILD_TESTS, and will replace true with ON and false with OFF.
  if [[ -n ${!var_name:-} ]]; then
    local cmake_var_name
    cmake_var_name=YB_$( tr '[:lower:]' '[:upper:]' <<<"${var_name}" )
    local cmake_var_value
    case "${!var_name}" in
      true) cmake_var_value=ON ;;
      false) cmake_var_value=OFF ;;
      *) fatal "Invalid value of variable ${var_name}: ${!var_name}. Expected 'true' or 'false'."
    esac
    cmake_opts+=( "-D${cmake_var_name}=${cmake_var_value}" )

    # CMakeCache.txt will contain something like
    # YB_BUILD_ODYSSEY:UNINITIALIZED=ON
    # Only re-run CMake if we want to put a different value of this variable there.
    local cmake_cache_path="$BUILD_ROOT/CMakeCache.txt"
    if [[ ${force_no_run_cmake} == "false" ]] && (
         [[ ! -f $cmake_cache_path ]] ||
         ! grep -Eq "^${cmake_var_name}:[A-Z]*=${cmake_var_value}$" "$cmake_cache_path"
       ); then
      force_run_cmake=true
    fi
  fi
}

use_packaged_targets() {
  local packaged_targets=()
  local optional_components_args=()
  if [[ "${build_odyssey:-}" == "true" ]]; then
    optional_components_args+=( "--with_odyssey" )
  else
    optional_components_args+=( "--no_odyssey" )
  fi
  if [[ "${build_yugabyted_ui:-}" == "true" ]]; then
    optional_components_args+=( "--with_yugabyted_ui" )
  else
    optional_components_args+=( "--no_yugabyted_ui" )
  fi
  while IFS='' read -r line; do
    packaged_targets+=( "$line" )
  done < <(
    activate_virtualenv &>/dev/null
    set_pythonpath
    "$YB_SCRIPT_PATH_LIST_PACKAGED_TARGETS" "${optional_components_args[@]}"
  )
  if [[ ${#packaged_targets[@]} -eq 0 ]]; then
    fatal "Failed to identify the set of targets to build for the release package"
  fi
  make_targets+=(
    "${packaged_targets[@]}"
    initial_sys_catalog_snapshot
    update_ysql_conn_mgr_template
    update_ysql_migrations
  )
}

# -------------------------------------------------------------------------------------------------
# Command line parsing
# -------------------------------------------------------------------------------------------------

build_type=""
verbose=false
force_run_cmake=false
force_no_run_cmake=false
remove_build_root_before_build=false
remove_entire_build_dir_before_build=false
clean_thirdparty=false
no_ccache=false
make_opts=()
force=false
build_cxx=true
build_java=true

# We will set this to true if we are running a single Java test or all tests.
run_java_tests=false

save_log=false
make_targets=()
no_tcmalloc=false
must_use_tcmalloc=false
cxx_test_name=""
test_existence_check=true
object_files_to_delete=()
should_run_ctest=false
ctest_args=""
num_test_repetitions=1
build_descriptor_path=""
export YB_GTEST_FILTER=""
repeat_unit_test_inherited_args=()
forward_args_to_repeat_unit_test=false
original_args=( "$@" )
user_mvn_opts=""
java_only=false
cmake_only=false
run_python_tests=false
cmake_extra_args=""
pgo_data_path=""
predefined_build_root=""
java_test_name=""
show_report=true
running_any_tests=false
clean_postgres=false
make_ninja_extra_args=""
java_lint=false
collect_java_tests=false
should_use_packaged_targets=false

# The default value of this parameter will be set based on whether we're running on Jenkins.
reduce_log_output=""

resolve_java_dependencies=false

run_cmake_unit_tests=false

run_shellcheck=false

should_build_clangd_index=false
clangd_index_format=binary

export YB_DOWNLOAD_THIRDPARTY=${YB_DOWNLOAD_THIRDPARTY:-1}
export YB_HOST_FOR_RUNNING_TESTS=${YB_HOST_FOR_RUNNING_TESTS:-}

export YB_EXTRA_GTEST_FLAGS=""
unset BUILD_ROOT

if [[ ${YB_RECREATE_INITIAL_SYS_CATALOG_SNAPSHOT:-} == "1" ]]; then
  log "Warning: re-setting externally passed value of YB_RECREATE_INITIAL_SYS_CATALOG_SNAPSHOT" \
      "back to 0 by default."
fi

export YB_RECREATE_INITIAL_SYS_CATALOG_SNAPSHOT=0

cxx_test_filter_regex=""
reset_cxx_test_filter=false

use_google_tcmalloc=""

# -------------------------------------------------------------------------------------------------
# Switches deciding what components or targets to build
# -------------------------------------------------------------------------------------------------

build_tests=""
build_fuzz_targets=""

# These will influence what targets to build if invoked with the packaged_targets meta-target.
build_odyssey=false
if is_linux; then
  build_odyssey=true
fi

build_yugabyted_ui=false

# -------------------------------------------------------------------------------------------------

validate_args_only=false

# -------------------------------------------------------------------------------------------------
# Actually parsing command-line arguments
# -------------------------------------------------------------------------------------------------

yb_build_args=( "$@" )

while [[ $# -gt 0 ]]; do
  if is_valid_build_type "$1"; then
    build_type="$1"
    shift
    continue
  fi
  if [[ ${forward_args_to_repeat_unit_test} == "true" ]]; then
    repeat_unit_test_inherited_args+=( "$1" )
    shift
    continue
  fi
  if [[ $1 =~ ^(--[a-z_-]+)=(.*)$ ]]; then
    set -- "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${@:2}"
  fi

  case ${1//_/-} in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --verbose)
      verbose=true
      export YB_VERBOSE=1
    ;;
    --bash-debug)
      yb_activate_debug_mode
    ;;
    --force-run-cmake|--frcm)
      force_run_cmake=true
    ;;
    --force-no-run-cmake|--fnrcm|--no-cmake|--skip-cmake)
      force_no_run_cmake=true
    ;;
    --cmake-only)
      cmake_only=true
    ;;
    --clean)
      set_clean_build
    ;;
    --clean-venv)
      YB_RECREATE_VIRTUALENV=1
    ;;
    --clean-all)
      set_clean_build
      remove_entire_build_dir_before_build=true
    ;;
    --clean-thirdparty)
      clean_thirdparty=true
      set_clean_build
    ;;
    -f|--force|-y)
      force=true
    ;;
    --clean-force|--cf|-cf)
      set_clean_build
      force=true
    ;;
    --no-ccache)
      no_ccache=true
    ;;
    --compiler-type)
      YB_COMPILER_TYPE=$2
      shift
    ;;
    # --clangd-* options have to precede the catch-all --clang* option that specifies compiler type.
    --clangd-index)
      enable_clangd_index_build
      build_java=false
    ;;
    --clangd-index-only|--cio)
      enable_clangd_index_build
      build_java=false
      disable_initdb
    ;;
    --clangd-index-format)
      clangd_index_format=$2
      shift
      validate_clangd_index_format "${clangd_index_format}"
      enable_clangd_index_build
    ;;
    --gcc)
      YB_COMPILER_TYPE="gcc"
    ;;
    --clang)
      YB_COMPILER_TYPE="clang"
    ;;
    --gcc*|--clang*)
      if [[ $1 =~ ^--(gcc|clang)[0-9]{1,2}$ ]]; then
        YB_COMPILER_TYPE=${1##--}
      else
        fatal "--gcc / --clang is expected to be followed by compiler major version"
      fi
    ;;
    --skip-java-build|--skip-java|--sjb|--sj|--no-java|--nj)
      build_java=false
    ;;
    --run-java-tests|--java-tests)
      run_java_tests=true
    ;;
    --save-log)
      save_log=true
    ;;
    --target)
      make_targets+=( "$2" )
      build_java=false
      shift
    ;;
    --targets)
      make_targets+=( "$2" )
      build_java=false
      shift
    ;;
    --no-tcmalloc)
      no_tcmalloc=true
      use_google_tcmalloc=false
    ;;
    --no-google-tcmalloc|--use-gperftools-tcmalloc|--gperftools-tcmalloc)
      use_google_tcmalloc=false
      must_use_tcmalloc=true
    ;;
    --use-google-tcmalloc|--google-tcmalloc)
      use_google_tcmalloc=true
      must_use_tcmalloc=true
    ;;
    --cxx-test|--ct)
      set_cxx_test_name "$2"
      shift
    ;;
    --test-args)
      ensure_option_has_arg "$@"
      export YB_EXTRA_GTEST_FLAGS+=" $2"
      shift
    ;;
    --java-test|--jt)
      set_java_test_name "$2"
      shift
    ;;
    --java-test-args)
      ensure_option_has_arg "$@"
      # Args passed over commandline take precedence over those set in environment variable.
      export YB_EXTRA_MVN_OPTIONS_IN_TESTS+=" $2"
      shift
    ;;
    --ctest)
      should_run_ctest=true
      running_any_tests=true
    ;;
    --ctest-args)
      should_run_ctest=true
      ctest_args+=$2
      shift
    ;;
    --no-rebuild-thirdparty|--nrtp|--nr3p|--nbtp|--nb3p)
      export NO_REBUILD_THIRDPARTY=1
    ;;
    --show-compiler-cmd-line|--sccl)
      export YB_SHOW_COMPILER_COMMAND_LINE=1
    ;;
    --skip-test-existence-check|--no-test-existence-check|--ntec) test_existence_check=false ;;
    --skip-check-test-existence|--no-check-test-existence|--ncte) test_existence_check=false ;;
    --gtest-filter)
      ensure_option_has_arg "$@"
      export YB_GTEST_FILTER=$2
      shift
    ;;
    # Support the way of argument passing that is used for gtest test programs themselves.
    --gtest-filter=*)
      export YB_GTEST_FILTER=${2#--gtest-filter=}
    ;;
    --rebuild-file)
      ensure_option_has_arg "$@"
      register_file_to_rebuild "$2"
      shift
    ;;
    --rebuild-target)
      ensure_option_has_arg "$@"
      object_files_to_delete+=( "$2.o" "$2.cc.o" )
      make_targets=( "$2" )
      shift
    ;;
    --generate-build-debug-scripts|--gen-build-debug-scripts|--gbds)
      export YB_GENERATE_BUILD_DEBUG_SCRIPTS=1
    ;;
    --skip-cxx-build|--scb)
      build_cxx=false
    ;;
    --java-only|--jo)
      build_cxx=false
      java_only=true
    ;;
    --build-yugabyted-ui|--with-yugabyted-ui)
      build_yugabyted_ui=true
    ;;
    --no-yugabyted-ui|--skip-yugabyted-ui)
      build_yugabyted_ui=false
    ;;
    --num-repetitions|--num-reps|-n)
      ensure_option_has_arg "$@"
      num_test_repetitions=$2
      shift
      if [[ ! $num_test_repetitions =~ ^[0-9]+$ ]]; then
        fatal "Invalid number of test repetitions: $num_test_repetitions"
      fi
    ;;
    --write-build-descriptor)
      ensure_option_has_arg "$@"
      build_descriptor_path=$2
      shift
    ;;
    --thirdparty-dir)
      ensure_option_has_arg "$@"
      export YB_THIRDPARTY_DIR=$2
      shift
      validate_thirdparty_dir
    ;;
    -j)
      ensure_option_has_arg "$@"
      export YB_MAKE_PARALLELISM=$2
      shift
    ;;
    -j[1-9]|-j[1-9][0-9]|-j[1-9][0-9][0-9]|-j[1-9][0-9][0-9][0-9])
      export YB_MAKE_PARALLELISM=${1#-j}
    ;;
    --remote)
      # We sometimes modify YB_REMOTE_COMPILATION in a subshell on purpose.
      # shellcheck disable=SC2031
      export YB_REMOTE_COMPILATION=1
      get_build_worker_list
    ;;
    --no-remote)
      export YB_REMOTE_COMPILATION=0
    ;;
    --collect-java-tests)
      collect_java_tests=true
    ;;
    --)
      if [[ $num_test_repetitions -lt 2 ]]; then
        fatal "Trying to forward arguments to repeat_unit_test.sh, but -n not specified, so" \
              "we won't be repeating a test multiple times."
      fi
      forward_args_to_repeat_unit_test=true
    ;;
    [a-z]*test)
      log "'$1' looks like a C++ test name, assuming --cxx-test"
      set_cxx_test_name "$1"
    ;;
    master|yb-master)
      make_targets+=( "yb-master" "gen_auto_flags_json" )
    ;;
    tserver|yb-tserver)
      make_targets+=( "yb-tserver" "gen_auto_flags_json" )
    ;;
    initdb)
      set_initdb_target
    ;;
    reinitdb)
      export YB_RECREATE_INITIAL_SYS_CATALOG_SNAPSHOT=1
      set_initdb_target
    ;;
    postgres)
      make_targets+=( "postgres" )
    ;;
    daemons|yb-daemons)
      make_targets+=( "yb-master" "yb-tserver" "gen_auto_flags_json" "postgres" "yb-admin" )
    ;;
    packaged|packaged-targets)
      should_use_packaged_targets=true
    ;;
    --skip-build|--sb)
      set_flags_to_skip_build
    ;;
    --mvn-opts)
      ensure_option_has_arg "$@"
      user_mvn_opts+=" $2"
      shift
    ;;
    --ninja)
      export YB_USE_NINJA=1
    ;;
    --make)
      export YB_USE_NINJA=0
    ;;
    --build-root)
      ensure_option_has_arg "$@"
      # predefined_build_root is used in a lot of places.
      # shellcheck disable=SC2034
      predefined_build_root=$2
      shift
    ;;
    --python-tests)
      run_python_tests=true
    ;;
    --shellcheck)
      run_shellcheck=true
    ;;
    --cmake-args)
      ensure_option_has_arg "$@"
      if [[ -n $cmake_extra_args ]]; then
        cmake_extra_args+=" "
      fi
      cmake_extra_args+=$2
      shift
    ;;
    --pgo-data-path)
      ensure_option_has_arg "$@"
      pgo_data_path=$(realpath "$2")
      shift
      if [[ ! -f $pgo_data_path ]]; then
        fatal "Profile data file doesn't exist: $pgo_data_path"
      fi
    ;;
    --make-ninja-extra-args)
      ensure_option_has_arg "$@"
      if [[ -n $make_ninja_extra_args ]]; then
        make_ninja_extra_args+=" "
      fi
      make_ninja_extra_args+=$2
      shift
    ;;
    --host-for-tests)
      ensure_option_has_arg "$@"
      export YB_HOST_FOR_RUNNING_TESTS=$2
      shift
    ;;
    --test-timeout-sec)
      ensure_option_has_arg "$@"
      export YB_TEST_TIMEOUT=$2
      if [[ ! $YB_TEST_TIMEOUT =~ ^[0-9]+$ ]]; then
        fatal "Invalid value for test timeout: '$YB_TEST_TIMEOUT'"
      fi
      shift
    ;;
    --sanitizer-extra-options|--extra-sanitizer-options)
      ensure_option_has_arg "$@"
      export YB_SANITIZER_EXTRA_OPTIONS=$2
      shift
    ;;
    --sanitizer-verbosity)
      ensure_option_has_arg "$@"
      YB_SANITIZER_EXTRA_OPTIONS=${YB_SANITIZER_EXTRA_OPTIONS:-}
      export YB_SANITIZER_EXTRA_OPTIONS+=" verbosity=$2"
      shift
    ;;
    --no-report)
      show_report=false
    ;;
    --tp|--test-parallelism)
      ensure_option_has_arg "$@"
      YB_TEST_PARALLELISM=$2
      validate_numeric_arg_range "test-parallelism" "$YB_TEST_PARALLELISM" \
        "$MIN_REPEATED_TEST_PARALLELISM" "$MAX_REPEATED_TEST_PARALLELISM"
      shift
    ;;
    --remove-successful-test-logs)
      export YB_REMOVE_SUCCESSFUL_JAVA_TEST_OUTPUT=1
    ;;
    --stop-at-failure|--stop-on-failure|--saf|--sof)
      repeat_unit_test_inherited_args+=( "$1" )
    ;;
    --stack-trace-error-status|--stes)
      export YB_STACK_TRACE_ON_ERROR_STATUS=1
    ;;
    --stack-trace-error-status-re|--stesr)
      ensure_option_has_arg "$@"
      export YB_STACK_TRACE_ON_ERROR_STATUS_RE=$2
      shift
    ;;
    --clean-postgres)
      clean_postgres=true
    ;;
    --no-postgres|--skip-postgres|--np|--sp)
      export YB_SKIP_POSTGRES_BUILD=1
    ;;
    --run-java-test-methods-separately|--rjtms)
      # We modify YB_RUN_JAVA_TEST_METHODS_SEPARATELY in a subshell in a few places on purpose.
      # shellcheck disable=SC2031
      export YB_RUN_JAVA_TEST_METHODS_SEPARATELY=1
      run_java_tests=true
    ;;
    --rebuild-postgres)
      clean_postgres=true
      make_targets+=( postgres )
    ;;
    --sanitizers-enable-coredump)
      export YB_SANITIZERS_ENABLE_COREDUMP=1
    ;;
    --valgrind-path)
      ensure_option_has_arg "$@"
      valgrind_path=$(realpath "$2")
      shift
      if [[ ! -f $valgrind_path ]]; then
        fatal "Valgrind file doesn't exist: $valgrind_path"
      fi
      export YB_VALGRIND_PATH=$valgrind_path
    ;;
    --extra-daemon-flags|--extra-daemon-args)
      ensure_option_has_arg "$@"
      log "Setting YB_EXTRA_DAEMON_FLAGS to: $2"
      export YB_EXTRA_DAEMON_FLAGS=$2
      shift
    ;;
    --java-lint)
      java_lint=true
    ;;
    --no-latest-symlink)
      export YB_DISABLE_LATEST_SYMLINK=1
    ;;
    --download-thirdparty|--dltp)
      export YB_DOWNLOAD_THIRDPARTY=1
    ;;
    --no-download-thirdparty|--ndltp)
      export YB_DOWNLOAD_THIRDPARTY=0
    ;;
    --super-bash-debug)
      # From https://wiki-dev.bash-hackers.org/scripting/debuggingtips
      export PS4='+(${BASH_SOURCE}:${LINENO}): ${FUNCNAME[0]:+${FUNCNAME[0]}(): }'
      # This is not a typo, we intend to log details of every statement in this mode:
      set -x
    ;;
    --reduce-log-output)
      reduce_log_output=true
    ;;
    --resolve-java-dependencies)
      resolve_java_dependencies=true
    ;;
    --no-tests|--skip-tests)
      build_tests=false
    ;;
    --with-tests)
      # We use this variable indirectly via propagate_bool_var_to_cmake.
      # shellcheck disable=SC2034
      build_tests=true
    ;;
    --no-fuzz-targets)
      build_fuzz_targets=false
    ;;
    --with-fuzz-targets)
      # We use this variable indirectly via propagate_bool_var_to_cmake.
      # shellcheck disable=SC2034
      build_fuzz_targets=true
    ;;
    --no-odyssey)
      build_odyssey=false
    ;;
    --with-odyssey)
      if is_mac; then
        fatal "Cannot build Odyssey on macOS"
      fi
      # We use this variable indirectly via propagate_bool_var_to_cmake.
      # shellcheck disable=SC2034
      build_odyssey=true
    ;;
    --enable-ysql-conn-mgr-test)
      export YB_ENABLE_YSQL_CONN_MGR_IN_TESTS=true
    ;;
    --cmake-unit-tests)
      run_cmake_unit_tests=true
    ;;
    --thin-lto)
      export YB_LINKING_TYPE=thin-lto
    ;;
    --full-lto)
      export YB_LINKING_TYPE=full-lto
    ;;
    --lto)
      if [[ ! $2 =~ ^(thin|full|none)$ ]]; then
        fatal "Invalid LTO type: $2"
      fi
      if [[ $2 == "none" ]]; then
        export YB_LINKING_TYPE=dynamic
      else
        export YB_LINKING_TYPE=$2-lto
      fi
      shift
    ;;
    --no-lto)
      export YB_LINKING_TYPE=dynamic
    ;;
    --export-compile-commands|--ccmds)
      export YB_EXPORT_COMPILE_COMMANDS=1
    ;;
    --export-compile-commands-cxx-only|--ccmdscxx)
      export YB_EXPORT_COMPILE_COMMANDS=1
      # This will skip time-consuming compile database generation for Postgres code. See
      # build_postgres.py for details.
      export YB_SKIP_PG_COMPILE_COMMANDS=1
    ;;
    --arch)
      if [[ -n ${YB_TARGET_ARCH:-} && "${YB_TARGET_ARCH}" != "$2" ]]; then
        log "Warning: YB_TARGET_ARCH is already set to ${YB_TARGET_ARCH}, setting to $2."
      fi
      export YB_TARGET_ARCH=$2
      shift
    ;;
    --linuxbrew)
      export YB_USE_LINUXBREW=1
    ;;
    --no-linuxbrew)
      export YB_USE_LINUXBREW=0
    ;;
    --no-initdb|--skip-initdb)
      disable_initdb
    ;;
    --skip-test-log-rewrite)
      export YB_SKIP_TEST_LOG_REWRITE=1
    ;;
    --skip-final-lto-link)
      export YB_SKIP_FINAL_LTO_LINK=1
    ;;
    --cxx-test-filter-re|--cxx-test-filter-regex)
      cxx_test_filter_regex=$2
      shift
    ;;
    --reset-cxx-test-filter)
      reset_cxx_test_filter=true
    ;;
    --validate-args-only)
      validate_args_only=true
    ;;
    *)
      if [[ $1 =~ ^(YB_[A-Z0-9_]+|postgres_FLAGS_[a-zA-Z0-9_]+)=(.*)$ ]]; then
        env_var_name=${BASH_REMATCH[1]}
        # Use "the ultimate fix" from http://bit.ly/setenvvar to set a variable with the name stored
        # in another variable to the given value.
        env_var_value=${BASH_REMATCH[2]}

        eval export "$env_var_name"=\$env_var_value  # note escaped dollar sign
        log "Setting $env_var_name to: '$env_var_value' (as specified on the command line)"
        unset env_var_name
        unset env_var_value
        shift
        continue
      fi

      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

# -------------------------------------------------------------------------------------------------
# Finished parsing command-line arguments, post-processing them.
# -------------------------------------------------------------------------------------------------

if is_apple_silicon && [[ -z ${YB_TARGET_ARCH:-} ]]; then
  # Use arm64 by default on an Apple Silicon machine.
  YB_TARGET_ARCH=arm64
fi

detect_architecture

set +u  # because yb_build_args might be empty
rerun_script_with_arch_if_necessary "$0" "${yb_build_args[@]}"
set -u

if [[ ${run_cmake_unit_tests} == "true" ]]; then
  # We don't even need the build root for these kinds of tests.
  log "--cmake-unit-tests specified, only running CMake tests"
  run_cmake_unit_tests

  exit
fi

if [[ -n $YB_GTEST_FILTER && -z $cxx_test_name ]]; then
  test_name=${YB_GTEST_FILTER%%.*}
  # Fix tests with non standard naming.
  if [[ $test_name == "CppCassandraDriverTest" ]]; then
    test_name="cassandracppdrivertest";
  fi
  set_cxx_test_name "GTEST_${test_name,,}"
fi

decide_whether_to_use_ninja
handle_predefined_build_root

# Setting CMake options.
cmake_opts=()

if is_mac && [[ $should_build_clangd_index == "true" && ${YB_COMPILER_TYPE:-} == "" ]]; then
  # On macOS, we need to use our custom-built version of Clang to build the clangd index.
  YB_COMPILER_TYPE=clang16
fi
if [[ $validate_args_only == "true" ]]; then
  yb_set_build_type_quietly=true
fi
set_cmake_build_type_and_compiler_type

if [[ $should_build_clangd_index == "true" && ! ${YB_COMPILER_TYPE} =~ ^clang[0-9]+$ ]]; then
  fatal "Cannot build clangd index with compiler type: ${YB_COMPILER_TYPE}." \
        "Use a version of Clang that includes clangd-indexer (specify --clang<version>)."
fi

if [[ -n ${cxx_test_filter_regex} ]]; then
  if [[ ${reset_cxx_test_filter} == "true" ]]; then
    fatal "--cxx-test-filter-regex is incompatible with --reset-cxx-filter-regex"
  fi
  set_cxx_test_filter_regex "${cxx_test_filter_regex}"
fi
if [[ ${reset_cxx_test_filter} == "true" ]]; then
  set_cxx_test_filter_regex ""
fi

if [[ $validate_args_only == "false" ]]; then
  log "YugabyteDB build is running on host '$HOSTNAME'"
  log "YB_COMPILER_TYPE=$YB_COMPILER_TYPE"
fi

normalize_build_type
if [[ ${verbose} == "true" ]]; then
  log "build_type=$build_type, cmake_build_type=$cmake_build_type"
fi
export BUILD_TYPE=$build_type

if [[ -z "${use_google_tcmalloc:-}" ]]; then
  if is_linux && [[ ! ${build_type} =~ ^(asan|tsan)$ ]]; then
    use_google_tcmalloc=true
  else
    use_google_tcmalloc=false
  fi
fi

if [[ ${force_run_cmake} == "true" && ${force_no_run_cmake} == "true" ]]; then
  fatal "--force-run-cmake and --force-no-run-cmake are incompatible"
fi

if [[ $cmake_only == "true" && ( $force_no_run_cmake == "true" || $java_only == "true" ) ]]; then
  fatal "--cmake-only is incompatible with --force-no-run-cmake or --java-only"
fi

if [[ ${should_run_ctest} == "true" ]]; then
  if [[ -n $cxx_test_name ]]; then
    fatal "--cxx-test (running  one C++ test) is mutually exclusive with" \
          "--ctest (running a number of C++ tests)"
  fi
  if [[ -n $java_test_name ]]; then
    fatal "--java-test (running one Java test) is mutually exclusive with " \
          "--ctest (running a number of C++ tests)"
  fi
  if [[ $run_java_tests == "false" ]]; then
    build_java=false
  fi
fi

if [[ $num_test_repetitions -lt 1 ]]; then
  fatal "Invalid number of test repetitions: $num_test_repetitions. Must be 1 or more."
fi

if [[ ${java_only} == "true" && ${build_java} == "false" ]]; then
  fatal "--java-only specified along with an option that implies skipping the Java build, e.g." \
        "--cxx-test or --skip-java-build."
fi

if [[ ${run_python_tests} == "true" ]]; then
  if [[ ${java_only} == "true" ]]; then
    fatal "The options --java-only and --python-tests are incompatible"
  fi
  log "--python-tests specified, only running Python tests"
  run_python_tests
  exit
fi

if [[ $run_shellcheck == "true" ]]; then
  run_shellcheck
fi

if [[ -n $java_test_name ]]; then
  if [[ -n $cxx_test_name ]]; then
    fatal "Cannot run a Java test and a C++ test at the same time"
  fi
  if $should_run_ctest; then
    fatal "Cannot run a Java test and ctest at the same time"
  fi
fi

if [[ ${YB_SKIP_BUILD:-} == "1" ]]; then
  log "YB_SKIP_BUILD is set, skipping all types of compilation"
  set_flags_to_skip_build
fi

configure_remote_compilation

if [[ ${java_lint} == "true" ]]; then
  log "--java-lint specified, only linting java code and then exiting."
  lint_java_code
  exit
fi

if ! is_jenkins && is_src_root_on_nfs && \
  [[ -z ${YB_CCACHE_DIR:-} && $HOME =~ $YB_NFS_PATH_RE ]]; then
  export YB_CCACHE_DIR=$HOME/.ccache
  if [[ ${build_cxx} == "true" ]]; then
    log "Setting YB_CCACHE_DIR=$YB_CCACHE_DIR by default for NFS-based builds"
  fi
fi

if [[ -z $reduce_log_output ]]; then
  if is_jenkins; then
    reduce_log_output=true
  else
    reduce_log_output=false
  fi
fi

if [[ ${build_java} != "true" && ${resolve_java_dependencies} == "true" ]]; then
  fatal "--resolve-java-dependencies is not allowed if not building Java code"
fi

if [[ $build_type == "prof_use" ]] && [[ $pgo_data_path == "" ]]; then
  fatal "Please set --pgo-data-path path/to/pgo/data"
fi

if [[ "${should_use_packaged_targets}" == "true" ]]; then
  use_packaged_targets
fi

if should_run_java_test_methods_separately && [[ -n $java_test_name && $java_test_name != *\#* ]]
then
  fatal "--run-java-test-methods-separately is specified, but the Java test name" \
        "${java_test_name} does not specify a particular test method to run." \
        "Please specify a test method using the following format: " \
        "--java-test <class_name>#testMethodName" \
        " or remove the --run-java-test-methods-separately flag. " \
        "To run all Java tests, replace --java-test=<test_name> with --java-tests."
fi

if [[ $validate_args_only == "true" ]]; then
  exit
fi

# End of post-processing and validating command-line arguments.

# -------------------------------------------------------------------------------------------------
# Recursively invoke this script in order to save the log to a file.
# -------------------------------------------------------------------------------------------------

if [[ ${save_log} == "true" ]]; then
  log_dir="$HOME/logs"
  mkdir_safe "$log_dir"
  log_name_prefix="$log_dir/${script_name}_${build_type}"
  log_path="${log_name_prefix}_$( date +%Y-%m-%d_%H_%M_%S ).log"
  latest_log_symlink_path="${log_name_prefix}_latest.log"
  rm -f "$latest_log_symlink_path"
  ln -s "$log_path" "$latest_log_symlink_path"

  print_saved_log_path

  filtered_args=()
  for arg in "${original_args[@]}"; do
    if [[ "$arg" != "--save-log" ]]; then
      filtered_args+=( "$arg" )
    fi
  done

  set +eu
  ( set -x; "$0" "${filtered_args[@]}" ) 2>&1 | tee "$log_path"
  exit_code=$?

  print_saved_log_path

  # No need to print a report here, because the recursive script invocation should have done so.
  exit "$exit_code"
fi

# -------------------------------------------------------------------------------------------------
# End of the section for supporting --save-log.
# -------------------------------------------------------------------------------------------------

# Some checks that can be performed before cleaning the build directory or identifying the
# third-party directory.
check_arc_wrapper

if [[ ${verbose} == "true" ]]; then
  log "$script_name command line: ${original_args[*]}"
fi

# shellcheck disable=SC2119
set_build_root

propagate_bool_var_to_cmake build_tests
propagate_bool_var_to_cmake build_fuzz_targets
propagate_bool_var_to_cmake build_odyssey

# -------------------------------------------------------------------------------------------------
# Cleaning confirmation
# ~~~~~~~~~~~~~~~~~~~~~
#
# If we are running in an interactive session, check if a clean build was done less than an hour
# ago. In that case, make sure this is what the user really wants.
# -------------------------------------------------------------------------------------------------

if tty -s && [[
    ${remove_build_root_before_build} == "true" ||
    ${remove_entire_build_dir_before_build} == "true" ||
    ${clean_thirdparty} == "true"
]]; then
  build_root_basename=${BUILD_ROOT##*/}
  last_clean_timestamp_path="$YB_SRC_ROOT/build/last_clean_timestamps/"
  last_clean_timestamp_path+="last_clean_timestamp__$build_root_basename"
  current_timestamp_sec=$( date +%s )
  if [ -f "$last_clean_timestamp_path" ]; then
    last_clean_timestamp_sec=$( cat "$last_clean_timestamp_path" )
    last_build_time_sec_ago=$(( current_timestamp_sec - last_clean_timestamp_sec ))
    if [[ "$last_build_time_sec_ago" -lt 3600 ]] && ! "$force"; then
      log "Last clean build on $build_root_basename was performed less than an hour" \
          "($last_build_time_sec_ago sec) ago."
      log "Do you still want to do a clean build? [y/N]"
      read -r answer
      if [[ ! "$answer" =~ ^[yY]$ ]]; then
        fatal "Operation canceled"
      fi
    fi
  fi
  mkdir -p "${last_clean_timestamp_path%/*}"
  echo "$current_timestamp_sec" >"$last_clean_timestamp_path"
fi

# -------------------------------------------------------------------------------------------------
# Cleaning
# -------------------------------------------------------------------------------------------------

if [[ ${remove_entire_build_dir_before_build} == "true" ]]; then
  log "Removing the entire ${YB_SRC_ROOT}/build directory (--clean-all specified)"
  ( set -x; rm -rf "${YB_SRC_ROOT}/build" )
  save_paths_and_archive_urls_to_build_dir
elif [[ ${remove_build_root_before_build} == "true" ]]; then
  log "Removing '$BUILD_ROOT' (--clean specified)"
  ( set -x; rm -rf "${BUILD_ROOT}" )
  save_paths_and_archive_urls_to_build_dir
else
  if [[ ${clean_postgres} == "true" ]]; then
    log "Removing contents of 'postgres_build' and 'postgres' subdirectories of '$BUILD_ROOT'"
    ( set -x; rm -rf "$BUILD_ROOT/postgres_build"/* "$BUILD_ROOT/postgres"/* )
  fi
fi

if [[ $clean_thirdparty == "true" ]] && using_default_thirdparty_dir; then
  log "Removing and re-building third-party dependencies (--clean-thirdparty specified)"
  (
    set -x
    "$YB_THIRDPARTY_DIR"/clean_thirdparty.sh --all
  )
fi

# -------------------------------------------------------------------------------------------------
# Done with cleaning, we can now download the third-party archive, determine toolchain, etc.
# -------------------------------------------------------------------------------------------------

find_or_download_ysql_snapshots
activate_virtualenv
set_pythonpath
find_or_download_thirdparty
detect_toolchain
find_make_or_ninja_and_update_cmake_opts
find_compiler_by_type
cmake_opts+=(
  "-DYB_RESOLVED_C_COMPILER=${cc_executable}"
  "-DYB_RESOLVED_CXX_COMPILER=${cxx_executable}"
)

if ! using_default_thirdparty_dir && [[ ${NO_REBUILD_THIRDPARTY:-0} != "1" ]]; then
  log "YB_THIRDPARTY_DIR ('$YB_THIRDPARTY_DIR') is not what we expect based on the source root " \
      "('$YB_SRC_ROOT/thirdparty'), not attempting to rebuild third-party dependencies."
  NO_REBUILD_THIRDPARTY=1
fi
export NO_REBUILD_THIRDPARTY

log_thirdparty_and_toolchain_details

validate_cmake_build_type "$cmake_build_type"

export YB_COMPILER_TYPE

if [[ ${verbose} == "true" ]]; then
  # http://stackoverflow.com/questions/22803607/debugging-cmakelists-txt
  cmake_opts+=( -Wdev --debug-output --trace -DYB_VERBOSE=1 )
  if ! using_ninja; then
    make_opts+=( VERBOSE=1 SH="bash -x" )
  fi
  export YB_SHOW_COMPILER_COMMAND_LINE=1
fi

mkdir_safe "$BUILD_ROOT"
cd "$BUILD_ROOT"

if ! using_ninja; then
  log "This build is NOT using Ninja. Consider specifying --ninja or setting YB_USE_NINJA=1."
fi

# Install the cleanup handler that will print a report at the end, even if we terminate with an
# error.
trap cleanup EXIT

check_python_script_syntax

set_java_home

if [[ ${no_ccache} == "true" ]]; then
  export YB_NO_CCACHE=1
fi

if [[ ${no_tcmalloc} == "true" && ${must_use_tcmalloc} == "true" ]]; then
  fatal "--no-tcmalloc was specified along with one of the options that implies we must use" \
        "some version of tcmalloc (Google tcmalloc or gperftools tcmalloc)"
fi

if [[ ${no_tcmalloc} == "true" ]]; then
  cmake_opts+=( -DYB_TCMALLOC_ENABLED=0 )
elif [[ -n ${YB_TCMALLOC_ENABLED:-} ]]; then
  cmake_opts+=( "-DYB_TCMALLOC_ENABLED=$YB_TCMALLOC_ENABLED" )
fi

if [[ ${use_google_tcmalloc} == "true" ]]; then
  if ! is_linux; then
    fatal "Google TCMalloc is only supported on linux. is_linux is: '${is_linux}'."
  fi
  cmake_opts+=( -DYB_GOOGLE_TCMALLOC=1 )
fi

if [[ $pgo_data_path != "" ]]; then
  cmake_opts+=( "-DYB_PGO_DATA_PATH=$pgo_data_path" )
fi

detect_num_cpus_and_set_make_parallelism
if [[ ${build_cxx} == "true" ]]; then
  log "Using make parallelism of $YB_MAKE_PARALLELISM" \
      "(YB_REMOTE_COMPILATION=${YB_REMOTE_COMPILATION:-undefined})"
fi

add_brew_bin_to_path

create_build_descriptor_file

create_build_root_file

if [[ ${#make_targets[@]} -eq 0 && -n $java_test_name ]]; then
  # Build only a subset of targets when we're only trying to run a Java test.
  make_targets+=( yb-master yb-tserver gen_auto_flags_json postgres update_ysql_conn_mgr_template
      update_ysql_migrations )
fi

if [[ $build_type == "compilecmds" ]]; then
  if [[ ${#make_targets[@]} -eq 0 ]]; then
    make_targets+=( gen_proto postgres yb_bfpg yb_bfql ql_parser_flex_bison_output)
    disable_initdb
  else
    log "Custom targets specified for a compilecmds build, not adding default targets"
  fi
  # We need to add anything that generates header files:
  # - Protobuf
  # - Built-in functions for YSQL and YCQL
  # - YCQL parser flex and bison output files.
  # If we don't do this, we'll get indexing errors in the files relying on the generated headers.
  #
  # Also we need to add postgres as a dependency, because it goes through the build_postgres.py
  # script and that is where the top-level combined compile_commands.json file is created.
  build_java=false
fi

readonly build_java=${build_java}

if [[ ${build_cxx} == "true" ||
      ${force_run_cmake} == "true" ||
      ${cmake_only} == "true" ||
      ( "${YB_EXPORT_COMPILE_COMMANDS:-}" == "1" &&
        ! -f "${BUILD_ROOT}/compile_commands.json" ) ]]; then
  run_cxx_build
fi

if [[ ${build_yugabyted_ui} == "true" && ${cmake_only} != "true" ]]; then
  run_yugabyted_ui_build
fi

export YB_JAVA_TEST_OFFLINE_MODE=0

if [[ -n $user_mvn_opts ]]; then
  # We do not double-quote $user_mvn_opts on purpose to allow multiple options.
  # shellcheck disable=SC2206
  java_build_common_opts=( $user_mvn_opts )
else
  java_build_common_opts=()
fi
# shellcheck disable=SC2206
user_mvn_opts_for_java_test=( $user_mvn_opts )

java_build_common_opts+=( install -DbinDir="$BUILD_ROOT/bin" )

# Build Java code and prepare for running the tests, if necessary, but do not run them yet.
if [[ ${build_java} == "true" ]]; then
  # We'll need this for running Java tests.
  set_sanitizer_runtime_options
  set_mvn_parameters

  java_build_opts=(
    "${java_build_common_opts[@]}"
    # Build tests but do not run them. We always run tests as a separate step from building the
    # code.
    -DskipTests
  )
  if [[ $run_java_tests == "true" && -n $java_test_name ]]; then
    # Assembly jars are jars that contain all dependencies. It takes a long time to build these,
    # and in general we don't need them when running tests, so skip them in the most common
    # development workflow when running a single test.
    java_build_opts+=( -Dassembly.skipAssembly=true )
  fi

  if [[ ${resolve_java_dependencies} == "true" ]]; then
    java_build_opts+=( "${MVN_OPTS_TO_DOWNLOAD_ALL_DEPS[@]}" )
  fi

  capture_sec_timestamp java_build_start
  for java_project_dir in "${yb_java_project_dirs[@]}"; do
    time (
      cd "$java_project_dir"
      build_yb_java_code "${java_build_opts[@]}"
    )
  done
  unset java_project_dir
  capture_sec_timestamp java_build_end

  if [[ $collect_java_tests == "true" ]]; then
    capture_sec_timestamp collect_java_tests_start
    collect_java_tests
    capture_sec_timestamp collect_java_tests_end
  fi

  log "Java build finished, total time information above."
fi

run_tests_remotely

if [[ ${ran_tests_remotely} != "true" ]]; then
  ensure_test_tmp_dir_is_set
  if [[ -n $cxx_test_name ]]; then
    capture_sec_timestamp cxx_test_start
    run_cxx_test
    capture_sec_timestamp cxx_test_end
  fi

  capture_sec_timestamp java_test_start
  if [[ -n $java_test_name ]]; then
    (
      if [[ $java_test_name == *\#* ]]; then
        # We are modifying this in a subshell. Shellcheck might complain about this elsewhere.
        export YB_RUN_JAVA_TEST_METHODS_SEPARATELY=1
      fi
      resolve_and_run_java_test "$java_test_name"
    )
  elif [[ $run_java_tests == "true" ]]; then
    if should_run_java_test_methods_separately; then
      if ! run_all_java_test_methods_separately; then
        log "Some Java tests failed"
        global_exit_code=1
      fi
    else
      java_code_build_purpose="running tests"
      java_test_opts=(
        "${java_build_common_opts[@]}"
        # To keep running tests in all Maven modules even after some tests fail.
        --fail-never
      )
      for java_project_dir in "${yb_java_project_dirs[@]}"; do
        time (
          cd "$java_project_dir"
          build_yb_java_code "${java_test_opts[@]}"
        )
      done
      unset java_project_dir
      unset java_code_build_purpose
    fi
  fi
  capture_sec_timestamp java_test_end

  if [[ ${should_run_ctest} == "true" ]]; then
    capture_sec_timestamp ctest_start
    run_ctest
    capture_sec_timestamp ctest_end
  fi
fi

if [[ ${should_build_clangd_index} == "true" ]]; then
  "${YB_BUILD_SUPPORT_DIR}/create_latest_symlink.sh" \
    "${BUILD_ROOT}" "${YB_BUILD_PARENT_DIR}/latest-for-clangd"
  build_clangd_index "${clangd_index_format}"
fi

# global_exit_code is declared with "-i" so it is always an integer.
exit ${global_exit_code}
