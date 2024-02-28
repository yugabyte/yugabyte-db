# shellcheck disable=SC2034

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

set_clean_build() {
  # We use is_clean_build in common-build-env.sh.
  # shellcheck disable=SC2034
  is_clean_build=true
  remove_build_root_before_build=true
}

yb_build_show_help() {
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

Configuration files:
  ~/.yb_buildrc (global to the user) and .git/yb_buildrc (for the specific source directory)

  These files can specify yb_build_prepend_args and yb_build_append_args arrays that specify
  default arguments to be added before and after user-specified arguments. Also these files can
  specify default values of variables that are populated as a result of command line parsing.

Setting YB_... environment variables on the command line:
  YB_SOME_VARIABLE1=some_value1 YB_SOME_VARIABLE2=some_value2
The same also works for postgres_FLAGS_... variables.

---------------------------------------------------------------------------------------------------

EOT
}

set_default_yb_build_args() {
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

  # -----------------------------------------------------------------------------------------------
  # Switches deciding what components or targets to build
  # -----------------------------------------------------------------------------------------------

  build_tests=""
  build_fuzz_targets=""

  # These will influence what targets to build if invoked with the packaged_targets meta-target.
  build_odyssey=false
  if is_linux; then
    build_odyssey=true
  fi

  build_yugabyted_ui=false

  validate_args_only=false
}

parse_yb_build_cmd_line() {
  yb_build_args+=( "$@" )

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
        yb_build_show_help >&2
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
      # --clangd-* options have to precede the catch-all --clang* option that specifies compiler
      # type.
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
          # Use "the ultimate fix" from http://bit.ly/setenvvar to set a variable with the name
          # stored in another variable to the given value.
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

}
