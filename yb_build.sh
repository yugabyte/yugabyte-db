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

. "${BASH_SOURCE%/*}"/build-support/common-test-env.sh

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} [<options>] [<build_type>] [<target_keywords>]
Options:
  -h, --help
    Show help.
  --verbose
    Show debug output from CMake.
  --force-run-cmake, --frcm
    Ensure that we explicitly invoke CMake from this script. CMake may still run as a result of
    changes made to CMakeLists.txt files if we just invoke make on the CMake-generated Makefile.
  --force-no-run-cmake, --fnrcm
    The opposite of --force-run-cmake. Makes sure we do not run CMake.
  --cmake-only
    Only run CMake, don't run any other build steps.
  --clean
    Remove the build directory before building.
  --clean-thirdparty
    Remove previously built third-party dependencies and rebuild them. Does not imply --clean.
  --no-ccache
    Do not use ccache. Useful when debugging build scripts or compiler/linker options.
  --clang
    Use the clang C/C++ compiler.
  --skip-build, --sb
    Skip all kinds of build (C++, Java)
  --skip-java-build, --skip-java, --sjb, --sj
    Do not package and install java source code.
  --java-tests, run-java-tests
    Run the java unit tests when build is enabled.
  --static
    Force a static build.
  --target, --targets
    Pass the given target or set of targets to make.
  --cxx-test <cxx_test_program_name>
    Build and run the given C++ test program. We run the test using ctest. Specific tests within the
    test program can be chosen using --gtest_filter.
  --java-test <java_test_name>
    Build and run the given Java test. Test name format is e.g.
    org.yb.loadtester.TestRF1Cluster[#testRF1toRF3].
  --no-tcmalloc
    Do not use tcmalloc.
  --no-rebuild-thirdparty, --nbtp, --nb3p, --nrtp, --nr3p
    Skip building third-party libraries, even if the thirdparty directory has changed in git.
  --use-shared-thirdparty, --ustp, --stp, --us3p, --s3p
    Try to find and use a shared third-party directory (in YugaByte's build environment these
    third-party directories are under $NFS_PARENT_DIR_FOR_SHARED_THIRDPARTY)
  --show-compiler-cmd-line, --sccl
    Show compiler command line.
  --{no,skip}-{test-existence-check,check-test-existence}
    Don't check that all test binaries referenced by CMakeLists.txt files exist.
  --gtest_filter
    Use the given filter to select Google Test tests to run. Uses with --cxx-test.
  --test-args
    Extra arguments to pass to the test. Used with --cxx-test.
  --rebuild-file <source_file_to_rebuild>
    The .o file corresponding to the given source file will be deleted from the build directory
    before the build.
  --rebuild-file <target_name>
    Combines --target and --rebuild-file. Currently only works if target name matches the object
    file name to be deleted.
  --generate-build-debug-scripts, --gen-build-debug-scripts, --gbds
    Specify this to generate one-off shell scripts that could be used to re-run and understand
    failed compilation commands.
  --ctest
    Runs ctest in the build directory after the build. This is mutually exclusive with --cxx-test.
    This will also skip building Java code, unless --run-java-tests is specified.
  --ctest-args
    Specifies additional arguments to ctest. Implies --ctest.
  --skip-cxx-build, --scb
    Skip C++ build. This is useful when repeatedly debugging tests using this tool and not making
    any changes to the code.
  --num-repetitions, --num-reps, -n
    Repeat a C++ test this number of times. This delegates to the repeat_unit_test.sh script.
  --write-build-descriptor <build_descriptor_path>
    Write a "build descriptor" file. A "build descriptor" is a YAML file that provides information
    about the build root, compiler used, etc.
  --force, -f, -y
    Run a clean build without asking for confirmation even if a clean build was recently done.
  --with-assembly
    Build the java code with assembly (basically builds the yb-sample-apps.jar as well)
  -j <parallelism>, -j<parallelism>
    Build using the given number of concurrent jobs (defaults to the number of CPUs).
  --remote
    Prefer a remote build on an auto-scaling cluster of build workers. The parallelism is picked
    automatically based on the current number of build workers.
  --thirdparty-dir <thirdparty_dir>
    Use a third-party directory other than <source_tree_root>/thirdparty. This is useful when using
    multiple build directories with different versions of YB code so we can avoid building
    third-party code multiple times.
  --mvn-opts <maven_options>
    Specify additional Maven options for Java build/tests.
  --java-only, --jo
    Only build Java code
  --ninja
    Use the Ninja backend instead of Make for CMake. This provides faster build speed in case
    most part of the code is already built.
  --make
    Use the Make backend (as opposed to Ninja).
  --build-root
    The build root directory, e.g. build/debug-gcc-dynamic-enterprise. This is used in scripting
    and is checked against other parameters.
  --python-tests
    Run various Python tests (doctest, unit test)
  --cotire
    Enable precompiled headers using cotire.
  --cmake-args
    Additional CMake arguments
  --host-for-tests
    Use this host for running tests. Could also be set using the YB_HOST_FOR_RUNNING_TESTS env
    variable.
  --test-timeout-sec
    Test timeout in seconds
  --sanitizer-extra-options, --extra-sanitizer-options
    Extra options to pass to ASAN/LSAN/UBSAN/TSAN. See https://goo.gl/VbTjHH for possible values.
  --sanitizer-verbosity
    Use the given verbosity value for clang sanitizers. The default is 0.
  --test-parallelism, --tp N
    When running tests repeatedly, run up to N instances of the test in parallel. Equivalent to the
    --parallelism argument of repeat_unit_test.sh.
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
  --
    Pass all arguments after -- to repeat_unit_test.
Build types:
  debug (default), fastdebug, release, profile_gen, profile_build, asan, tsan
Supported target keywords:
  ...-test           - build and run a C++ test
  [yb-]master        - master executable
  [yb-]tserver       - tablet server executable
  daemons            - both yb-master and yb-tserver
  packaged[-targets] - targets that are required for a release package
EOT
}

set_cxx_test_name() {
  expect_num_args 1 "$@"
  if [[ $cxx_test_name == $1 ]]; then
    # Duplicate test name specified, ignore.
    return
  fi
  if [[ -n $cxx_test_name ]]; then
    fatal "Only one C++ test name can be specified (found '$cxx_test_name' and '$1')."
  fi
  cxx_test_name=$1
  running_any_tests=true
  build_java=false
}

set_java_test_name() {
  expect_num_args 1 "$@"
  if [[ $java_test_name == $1 ]]; then
    # Duplicate test name specified, ignore.
    return
  fi
  if [[ -n $java_test_name ]]; then
    fatal "Only one Java test name can be specified (found '$java_test_name' and '$1')."
  fi
  running_any_tests=true
  java_test_name=$1
}

set_vars_for_cxx_test() {
  make_targets+=( $cxx_test_name )

  # This is necessary to avoid failures if we are just building one test.
  test_existence_check=false
}

print_report_line() {
  local format_suffix=$1
  shift
  printf '%-32s : '"$format_suffix\n" "$@"
}

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
    print_report_line "%d seconds" "$caption" "$(( $end_time - $start_time ))"
  fi
}

print_report() {
  if "$show_report"; then
    (
      echo
      thick_horizontal_line
      echo "YUGABYTE BUILD SUMMARY"
      thick_horizontal_line
      print_report_line "%s" "Build type" "${build_type:-undefined}"
      print_report_line "%s" "Edition" "${YB_EDITION:-undefined}"
      report_time "CMake" "cmake"
      report_time "C++ compilation" "make"
      report_time "Java compilation" "java_build"
      report_time "C++ (one test program)" "cxx_test"
      report_time "ctest (multiple C++ test programs)" "ctest"
      report_time "Remote tests" "remote_tests"
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
cmake_build_type: "$cmake_build_type"
build_root: "$BUILD_ROOT"
compiler_type: "$YB_COMPILER_TYPE"
thirdparty_dir: "${YB_THIRDPARTY_DIR:-$YB_SRC_ROOT/thirdparty}"
EOT
    log "Created a build descriptor file at '$build_descriptor_path'"
  fi
}

capture_sec_timestamp() {
  expect_num_args 1 "$@"
  local current_timestamp=$(date +%s)
  eval "${1}_time_sec=$current_timestamp"
}

run_cxx_build() {
  if ( "$force_run_cmake" || "$cmake_only" || [[ ! -f $make_file ]] ) && \
     ! "$force_no_run_cmake"; then
    if [[ -z ${NO_REBUILD_THIRDPARTY:-} ]]; then
      build_compiler_if_necessary
    fi
    log "Using cmake binary: $( which cmake )"
    log "Running cmake in $PWD"
    capture_sec_timestamp "cmake_start"
    ( set -x; cmake "${cmake_opts[@]}" $cmake_extra_args "$YB_SRC_ROOT" )
    capture_sec_timestamp "cmake_end"
  fi

  if "$cmake_only"; then
    log "CMake has been invoked, stopping here (--cmake-only specified)."
    exit
  fi

  if [[ ${#object_files_to_delete[@]} -gt 0 ]]; then
    log_empty_line
    log "Deleting object files corresponding to: ${object_files_to_delete[@]}"
    # TODO: can delete multiple files using the same find command.
    for object_file_to_delete in "${object_files_to_delete[@]}"; do
      ( set -x; find "$BUILD_ROOT" -name "$object_file_to_delete" -exec rm -fv {} \; )
    done
    log_empty_line
  fi

  log "Running $make_program in $PWD"
  capture_sec_timestamp "make_start"
  set +u +e  # "set -u" may cause failures on empty lists
  time (
    set -x

    "$make_program" "-j$YB_MAKE_PARALLELISM" "${make_opts[@]}" "${make_targets[@]}"
  )

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
  if "$test_existence_check" && [[ ${#make_targets[@]} -eq 0 ]]; then
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

run_ctest() {
  # Not setting YB_CTEST_VERBOSE here because we don't want the output of a potentially large number
  # of tests to go to stdout.
  (
    cd "$BUILD_ROOT"
    set -x
    ctest -j"$YB_NUM_CPUS" --verbose $ctest_args 2>&1 |
      egrep -v "^[0-9]+: Test timeout computed to be: "
  )
}

run_tests_remotely() {
  ran_tests_remotely=false
  if ! "$running_any_tests"; then
    # Nothing to run remotely.
    return
  fi
  if [[ -n ${YB_HOST_FOR_RUNNING_TESTS:-} && \
        $YB_HOST_FOR_RUNNING_TESTS != "localhost" && \
        $YB_HOST_FOR_RUNNING_TESTS != $HOSTNAME && \
        $YB_HOST_FOR_RUNNING_TESTS != $HOSTNAME.* ]] ; then
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
        --clean|--clean-thirdparty)
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
    (
      export YB_COMPILER_TYPE
      set +u
      repeat_unit_test_extra_args=( "${repeat_unit_test_inherited_args[@]}" )
      set -u
      if [[ -n ${test_parallelism:-} ]]; then
        repeat_unit_test_extra_args+=( --parallelism "$test_parallelism" )
      fi
      if "$verbose"; then
        repeat_unit_test_extra_args+=( --verbose )
      fi
      set -x +u
      "$YB_SRC_ROOT"/bin/repeat_unit_test.sh "$build_type" "$test_binary_name" \
         --num-iter "$num_test_repetitions" "${repeat_unit_test_extra_args[@]}"
      set -u
    )
  fi
}

run_java_test() {
  local module_dir
  local language
  local java_test_method_name=${java_test_name##*#}
  local java_test_class=${java_test_name%#*}
  local rel_java_src_path=${java_test_class//./\/}

  local module_name=""
  for module_dir in "$YB_SRC_ROOT"/java/*; do
    if [[ -d $module_dir ]]; then
      for language in java scala; do
        if [[ -f $module_dir/src/test/$language/$rel_java_src_path.$language ]]; then
          module_name=${module_dir##*/}
        fi
      done
    fi
  done

  local IFS=$'\n'
  if [[ -z $module_name ]]; then
    # Could not find the test source assuming we are given the complete package. Let's assume we
    # only have the class name.
    module_name=""
    for module_dir in "$YB_SRC_ROOT"/java/*; do
      if [[ -d $module_dir ]]; then
        local module_test_src_root="$module_dir/src/test"
        if [[ -d $module_test_src_root && -n $(
                find "$module_test_src_root" \( -name "$java_test_class.java" -or \
                                                -name "$java_test_class.scala" \) \
              ) ]]; then
          local current_module_name=${module_dir##*/}
          if [[ -n $module_name ]]; then
            fatal "Could not determine module for Java/Scala test '$java_test_name': both" \
                  "'$module_name' and '$current_moudle_name' are valid candidates."
          fi
          module_name=$current_module_name
        fi
      fi
    done

    if [[ -z $module_name ]]; then
      fatal "Could not find module name for Java/Scala test '$java_test_name'"
    fi
  fi

  $YB_SRC_ROOT/build-support/run-test.sh "$module_name" "$java_test_name"
}

# -------------------------------------------------------------------------------------------------
# Command line parsing

build_type=""
verbose=false
force_run_cmake=false
force_no_run_cmake=false
clean_before_build=false
clean_thirdparty=false
no_ccache=false
make_opts=()
force=false
build_cxx=true
build_java=true
run_java_tests=false
save_log=false
make_targets=()
no_tcmalloc=false
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
java_with_assembly=false
mvn_opts=""
java_only=false
cmake_only=false
use_shared_thirdparty=false
run_python_tests=false
cmake_extra_args=""
predefined_build_root=""
java_test_name=""
show_report=true
running_any_tests=false

export YB_HOST_FOR_RUNNING_TESTS=${YB_HOST_FOR_RUNNING_TESTS:-}

export YB_EXTRA_GTEST_FLAGS=""
unset BUILD_ROOT

while [[ $# -gt 0 ]]; do
  if is_valid_build_type "$1"; then
    build_type="$1"
    shift
    continue
  fi
  if "$forward_args_to_repeat_unit_test"; then
    repeat_unit_test_inherited_args+=( "$1" )
    shift
    continue
  fi

  case "${1//_/-}" in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --verbose)
      verbose=true
      export YB_VERBOSE=1
    ;;
    --force-run-cmake|--frcm)
      force_run_cmake=true
    ;;
    --force-no-run-cmake|--fnrcm)
      force_no_run_cmake=true
    ;;
    --cmake-only)
      cmake_only=true
    ;;
    --clean)
      clean_before_build=true
    ;;
    --clean-thirdparty)
      clean_thirdparty=true
    ;;
    -f|--force|-y)
      force=true
    ;;
    --no-ccache)
      no_ccache=true
    ;;
    --gcc)
      YB_COMPILER_TYPE="gcc"
    ;;
    --clang)
      YB_COMPILER_TYPE="clang"
    ;;
    --zapcc)
      YB_COMPILER_TYPE="zapcc"
    ;;
    --skip-java-build|--skip-java|--sjb|--sj)
      build_java=false
    ;;
    --run-java-tests|--java-tests)
      run_java_tests=true
    ;;
    --with-assembly)
      java_with_assembly=true
    ;;
    --static)
      YB_LINK=static
    ;;
    --save-log)
      save_log=true
    ;;
    --target)
      make_targets+=( "$2" )
      shift
    ;;
    --targets)
      make_targets+=( $2 )
      shift
    ;;
    --no-tcmalloc)
      no_tcmalloc=true
    ;;
    --cxx-test|--ct)
      set_cxx_test_name "$2"
      shift
    ;;
    --java-test|--jt)
      set_java_test_name "$2"
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
    --use-shared-thirdparty|--ustp|--stp|--us3p|--s3p)
      use_shared_thirdparty=true
    ;;
    --show-compiler-cmd-line|--sccl)
      export YB_SHOW_COMPILER_COMMAND_LINE=1
    ;;
    --skip-test-existence-check|--no-test-existence-check|--ntec) test_existence_check=false ;;
    --skip-check-test-existence|--no-check-test-existence|--ncte) test_existence_check=false ;;
    --gtest-filter)
      export YB_GTEST_FILTER=$2
      shift
    ;;
    --rebuild-file)
      object_files_to_delete+=( "$2.o" "$2.cc.o" )
      shift
    ;;
    --test-args)
      export YB_EXTRA_GTEST_FLAGS+=" $2"
      shift
    ;;
    --rebuild-target)
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
    --num-repetitions|--num-reps|-n)
      num_test_repetitions=$2
      shift
      if [[ ! $num_test_repetitions =~ ^[0-9]+$ ]]; then
        fatal "Invalid number of test repetitions: $num_test_repetitions"
      fi
    ;;
    --write-build-descriptor)
      build_descriptor_path=$2
      shift
    ;;
    --thirdparty-dir)
      export YB_THIRDPARTY_DIR=$2
      shift
      validate_thirdparty_dir
    ;;
    -j)
      export YB_MAKE_PARALLELISM=$2
      shift
    ;;
    -j[1-9])
      export YB_MAKE_PARALLELISM=${1#-j}
    ;;
    --remote)
      export YB_REMOTE_BUILD=1
      if [[ ! -f "$YB_BUILD_WORKERS_FILE" ]]; then
        fatal "--remote specified but $YB_BUILD_WORKERS_FILE not found"
      fi
    ;;
    --no-remote)
      unset YB_REMOTE_BUILD
      export YB_NO_REMOTE_BUILD=1
    ;;
    --)
      if [[ $num_test_repetitions -lt 2 ]]; then
        fatal "Forward to arguments to repeat_unit_test.sh without multiple repetitions"
      fi
      forward_args_to_repeat_unit_test=true
    ;;
    [a-z]*test)
      log "'$1' looks like a C++ test name, assuming --cxx-test"
      set_cxx_test_name "$1"
    ;;
    master|yb-master)
      make_targets+=( "yb-master" )
    ;;
    tserver|yb-tserver)
      make_targets+=( "yb-tserver" )
    ;;
    daemons|yb-daemons)
      make_targets+=( "yb-master" "yb-tserver" )
    ;;
    packaged|packaged-targets)
      for packaged_target in $( "$YB_SRC_ROOT"/build-support/list_packaged_targets.py ); do
        make_targets+=( "$packaged_target" )
      done
      if [[ ${#make_targets[@]} -eq 0 ]]; then
        fatal "Failed to identify the set of targets to build for the release package"
      fi
    ;;
    --skip-build|--sb)
      set_flags_to_skip_build
    ;;
    --community|--comm)
      export YB_EDITION=community
    ;;
    --enterprise|--ent)
      export YB_EDITION=enterprise
    ;;
    --edition)
      export YB_EDITION=$2
      validate_edition
      shift
    ;;
    --mvn-opts)
      mvn_opts+=" $2"
      shift
    ;;
    --ninja)
      export YB_USE_NINJA=1
    ;;
    --make)
      unset YB_USE_NINJA
    ;;
    --build-root)
      predefined_build_root=$2
      shift
    ;;
    --python-tests)
      run_python_tests=true
    ;;
    --cotire)
      export YB_USE_COTIRE=1
      force_run_cmake=true
    ;;
    --cmake-args)
      if [[ -n $cmake_extra_args ]]; then
        cmake_extra_args+=" "
      fi
      cmake_extra_args+=$2
      shift
    ;;
    --host-for-tests)
      export YB_HOST_FOR_RUNNING_TESTS=$2
      shift
    ;;
    --test-timeout-sec)
      export YB_TEST_TIMEOUT=$2
      if [[ ! $YB_TEST_TIMEOUT =~ ^[0-9]+$ ]]; then
        fatal "Invalid value for test timeout: '$YB_TEST_TIMEOUT'"
      fi
      shift
    ;;
    --sanitizer-extra-options|--extra-sanitizer-options)
      export YB_SANITIZER_EXTRA_OPTIONS=$2
      shift
    ;;
    --sanitizer-verbosity)
      YB_SANITIZER_EXTRA_OPTIONS=${YB_SANITIZER_EXTRA_OPTIONS:-}
      export YB_SANITIZER_EXTRA_OPTIONS+=" verbosity=$2"
      shift
    ;;
    --no-report)
      show_report=false
    ;;
    --tp|--test-parallelism)
      test_parallelism=$2
      validate_numeric_arg_range "test-parallelism" "$test_parallelism" \
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
      export YB_STACK_TRACE_ON_ERROR_STATUS_RE=$2
      shift
    ;;
    *)
      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

handle_predefined_build_root

unset cmake_opts
set_cmake_build_type_and_compiler_type
log "YB_COMPILER_TYPE=$YB_COMPILER_TYPE"

if "$verbose"; then
  log "build_type=$build_type, cmake_build_type=$cmake_build_type"
fi

if "$force_run_cmake" && "$force_no_run_cmake"; then
  fatal "--force-run-cmake and --force-no-run-cmake are incompatible"
fi

if "$cmake_only" && [[ $force_no_run_cmake == "true" || $java_only == "true" ]]; then
  fatal "--cmake-only is incompatible with --force-no-run-cmake or --java-only"
fi

if "$should_run_ctest"; then
  if [[ -n $cxx_test_name ]]; then
    fatal "--cxx-test (running  one C++ test) is mutually exclusive with" \
          "--ctest (running a number of C++ tests)"
  fi
  if [[ -n $java_test_name ]]; then
    fatal "--java-test (running one Java test) is mutually exclusive with " \
          "--ctest (running a number of C++ tests)"
  fi
  if ! "$run_java_tests"; then
    build_java=false
  fi
fi

if [[ $num_test_repetitions -lt 1 ]]; then
  fatal "Invalid number of test repetitions: $num_test_repetitions. Must be 1 or more."
fi

if [[ ${YB_REMOTE_BUILD:-} == "1" && ${YB_NO_REMOTE_BUILD:-} == "1" ]]; then
  fatal "YB_REMOTE_BUILD (--remote) and YB_NO_REMOTE_BUILD (--no-remote) cannot be set" \
        "at the same time."
fi

if "$java_only" && ! "$build_java"; then
  fatal "--java-only specified along with an option that implies skipping the Java build, e.g." \
        "--cxx-test or --skip-java-build."
fi

if [[ -n ${YB_THIRDPARTY_DIR:-} && $YB_THIRDPARTY_DIR != "$YB_SRC_ROOT/thirdparty" ]]; then
  log "YB_THIRDPARTY_DIR ('$YB_THIRDPARTY_DIR') is not what we expect based on the source root " \
      "('$YB_SRC_ROOT/thirdparty'), not attempting to rebuild third-party dependencies."
  export NO_REBUILD_THIRDPARTY=1
fi

if "$run_python_tests"; then
  if "$java_only"; then
    fatal "The options --java-only and --python-tests are incompatible"
  fi
  log "--python-tests specified, only running Python tests"
  run_python_tests
  exit
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

if "$use_shared_thirdparty" || [[ -f $YB_SRC_ROOT/thirdparty/.yb_thirdparty_do_not_use ]]; then
  find_thirdparty_dir
fi

configure_remote_build
detect_edition

if "$save_log"; then
  log_dir="$HOME/logs"
  mkdir_safe "$log_dir"
  log_name_prefix="$log_dir/${script_name}_${build_type}"
  log_path="${log_name_prefix}_$( date +%Y-%m-%d_%H_%M_%S ).log"
  latest_log_symlink_path="${log_name_prefix}_latest.log"
  rm -f "$latest_log_symlink_path"
  ln -s "$log_path" "$latest_log_symlink_path"

  echo "Logging to $log_path (also symlinked to $latest_log_symlink_path)" >&2
  filtered_args=()
  for arg in "${original_args[@]}"; do
    if [[ "$arg" != "--save-log" ]]; then
      filtered_args+=( "$arg" )
    fi
  done

  set +eu
  ( set -x; "$0" "${filtered_args[@]}" ) 2>&1 | tee "$log_path"
  exit_code=$?
  echo "Log saved to $log_path (also symlinked to $latest_log_symlink_path)" >&2
  print_report
  exit "$exit_code"
fi

if "$verbose"; then
  log "$script_name command line: ${original_args[@]}"
fi

set_build_root
set_vars_for_cxx_test

validate_cmake_build_type "$cmake_build_type"

export YB_COMPILER_TYPE

if "$verbose"; then
  # http://stackoverflow.com/questions/22803607/debugging-cmakelists-txt
  cmake_opts+=( -Wdev --debug-output --trace -DYB_VERBOSE=1 )
  make_opts+=( VERBOSE=1 SH="bash -x" )
  export YB_SHOW_COMPILER_COMMAND_LINE=1
fi

# If we are running in an interactive session, check if a clean build was done less than an hour
# ago. In that case, make sure this is what the user really wants.
if tty -s && ( $clean_before_build || $clean_thirdparty ); then
  build_root_basename=${BUILD_ROOT##*/}
  last_clean_timestamp_path="$YB_SRC_ROOT/build/last_clean_timestamp__$build_root_basename"
  current_timestamp_sec=$( date +%s )
  if [ -f "$last_clean_timestamp_path" ]; then
    last_clean_timestamp_sec=$( cat "$last_clean_timestamp_path" )
    last_build_time_sec_ago=$(( $current_timestamp_sec - $last_clean_timestamp_sec ))
    if [[ "$last_build_time_sec_ago" -lt 3600 ]] && ! "$force"; then
      log "Last clean build on $build_root_basename was performed less than an hour" \
          "($last_build_time_sec_ago sec) ago."
      log "Do you still want to do a clean build? [y/N]"
      read answer
      if [[ ! "$answer" =~ ^[yY]$ ]]; then
        fatal "Operation canceled"
      fi
    fi
  fi
  mkdir -p "$YB_SRC_ROOT/build"
  echo "$current_timestamp_sec" >"$last_clean_timestamp_path"
fi

if "$clean_before_build"; then
  log "Removing '$BUILD_ROOT' (--clean specified)"
  ( set -x; rm -rf "$BUILD_ROOT" )
fi

mkdir_safe "$BUILD_ROOT"
mkdir_safe "thirdparty/installed/uninstrumented/include"
mkdir_safe "thirdparty/installed-deps/include"

cd "$BUILD_ROOT"

activate_virtualenv
check_python_interpreter_versions
check_python_script_syntax

if "$clean_thirdparty"; then
  log "Removing and re-building third-party dependencies (--clean-thirdparty specified)"
  (
    set -x
    "$YB_THIRDPARTY_DIR"/clean_thirdparty.sh --all
  )
fi

if "$no_ccache"; then
  export YB_NO_CCACHE=1
fi

if "$no_tcmalloc"; then
  cmake_opts+=( -DYB_TCMALLOC_AVAILABLE=0 )
fi

detect_num_cpus_and_set_make_parallelism
log "Using make parallelism of $YB_MAKE_PARALLELISM" \
    "(YB_REMOTE_BUILD=${YB_REMOTE_BUILD:-undefined})"

set_build_env_vars

create_build_descriptor_file

if [[ ${#make_targets[@]} -eq 0 && -n $java_test_name ]]; then
  # Only build yb-master / yb-tserver when we're only trying to run a Java test.
  make_targets+=( yb-master yb-tserver )
fi

if "$build_cxx" || "$force_run_cmake" || "$cmake_only"; then
  run_cxx_build
fi

# Check if the Java build is needed, and skip Java unit test runs if requested.
if "$build_java"; then
  # We'll need this for running Java tests.
  set_sanitizer_runtime_options

  cd "$YB_SRC_ROOT"/java
  build_opts=( install )
  if ! "$java_with_assembly"; then
    build_opts+=( -DskipAssembly )
  fi
  if "$run_java_tests"; then
    build_opts+=( -DbinDir="$BUILD_ROOT/bin" )
  else
    build_opts+=( -DskipTests )
  fi
  java_build_start_time_sec=$(date +%s)
  time ( build_yb_java_code $mvn_opts ${build_opts[@]} )
  java_build_end_time_sec=$(date +%s)
  log "Java build finished, total time information above."
fi

run_tests_remotely

if ! "$ran_tests_remotely"; then
  if [[ -n $cxx_test_name ]]; then
    capture_sec_timestamp cxx_test_start
    run_cxx_test
    capture_sec_timestamp cxx_test_end
  fi

  if [[ -n $java_test_name ]]; then
    run_java_test
  fi

  if "$should_run_ctest"; then
    capture_sec_timestamp ctest_start
    run_ctest
    capture_sec_timestamp ctest_end
  fi
fi

print_report
