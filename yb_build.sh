#!/usr/bin/env bash
set -euo pipefail

script_name=${0##*/}
script_name=${script_name%.*}

# Source the common-build-env.sh script in the build-support subdirectory of the directory of this
# script.
. "${0%/*}"/build-support/common-test-env.sh

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} [<options>] [<build_type>]
Options:
  -h, --help
    Show help.
  --verbose
    Show debug output from CMake.
  --force-run-cmake
    Ensure that we explicitly invoke CMake from this script. CMake may still run as a result of
    changes made to CMakeLists.txt files if we just invoke make on the CMake-generated Makefile.
  --clean
    Remove the build directory before building.
  --clean-thirdparty
    Remove previously built third-party dependencies and rebuild them. Does not imply --clean.
  --rocksdb-only
    Only build RocksDB code (all targets).
  --no-ccache
    Do not use ccache. Useful when debugging build scripts or compiler/linker options.
  --clang
    Use the clang C/C++ compiler.
  --skip-java-build, --skip-java, --sj
    Do not package and install java source code.
  --run-java-tests
    Run the java unit tests when build is enabled.
  --static
    Force a static build.
  --target
    Pass the given target to make.
  --cxx-test <test_name>
    Build and run the given C++ test. We run the test directly (not going through ctest).
  --gtest_filter <filter>
    This will be passed to the C++ test specified by --cxx-test.
  --no-tcmalloc
    Do not use tcmalloc.
  --no-rebuild-thirdparty
    Skip building third-party libraries, even if the thirdparty directory has changed in git.
  --show-compiler-cmd-line
    Show compiler command line.
  --skip-test-existence-check, --no-test-existence-check
    Don't check that all test binaries referenced by CMakeLists.txt files exist.
  --gtest-regex
    Use the given regular expression to filter tests within a gtest-based binary when running them
    with --cxx-test.
  --rebuild-file <source_file_to_rebuild>
    The .o file corresponding to the given source file will be deleted from the build directory
    before the build.

Build types:
  debug (default), fastdebug, release, profile_gen, profile_build, asan, tsan
EOT
}

build_type="debug"
build_type_specified=false
verbose=false
force_run_cmake=false
clean_before_build=false
clean_thirdparty=false
rocksdb_only=false
rocksdb_targets=""
no_ccache=false
make_opts=()
force=false
build_java=true
run_java_tests=false
save_log=false
make_targets=()
no_tcmalloc=false
cxx_test_name=""
test_existence_check=true
object_files_to_delete=()

unset YB_GTEST_REGEX

original_args=( "$@" )
while [ $# -gt 0 ]; do
  if is_valid_build_type "$1"; then
    build_type="$1"
    build_type_specified=true
    shift
    continue
  fi

  case "$1" in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    --verbose)
      verbose=true
    ;;
    --force-run-cmake)
      force_run_cmake=true
    ;;
    --clean)
      clean_before_build=true
    ;;
    --clean-thirdparty)
      clean_thirdparty=true
    ;;
    -f|--force)
      force=true
    ;;
    --rocksdb-only)
      rocksdb_only=true
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
    --skip-java-build|--skip-java|--sj)
      build_java=false
    ;;
    --run-java-tests)
      run_java_tests=true
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
    --no-tcmalloc)
      no_tcmalloc=true
    ;;
    --cxx-test)
      cxx_test_name="$2"
      make_targets+=( "$2" )
      build_java=false
      # This is necessary to avoid failures if we are just building one test.
      test_existence_check=false
      shift
    ;;
    --no-rebuild-thirdparty)
      export NO_REBUILD_THIRDPARTY=1
    ;;
    --show-compiler-cmd-line)
      export YB_SHOW_COMPILER_COMMAND_LINE=1
    ;;
    --skip-test-existence-check|--no-test-existence-check)
      test_existence_check=false
    ;;
    --gtest-regex)
      export YB_GTEST_REGEX="$2"
      shift
    ;;
    --rebuild-file)
      object_files_to_delete+=( "$2.o" )
      shift
    ;;
    rocksdb_*)
      # Assume this is a CMake target we've created for RocksDB tests.
      make_opts+=( "$1" )
    ;;
    *)
      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

unset cmake_opts
set_cmake_build_type_and_compiler_type

if "$verbose"; then
  log "build_type=$build_type, cmake_build_type=$cmake_build_type"
fi

if "$save_log"; then
  log_dir="$HOME/logs"
  mkdir -p "$log_dir"
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
  exit "$exit_code"
fi

if "$verbose"; then
  log "$script_name command line: ${original_args[@]}"
fi

set_build_root

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
  last_clean_timestamp_path="$YB_SRC_ROOT/build/last_clean_timestamp"
  current_timestamp_sec=$( date +%s )
  if [ -f "$last_clean_timestamp_path" ]; then
    last_clean_timestamp_sec=$( cat "$last_clean_timestamp_path" )
    last_build_time_sec_ago=$(( $current_timestamp_sec - $last_clean_timestamp_sec ))
    if [[ "$last_build_time_sec_ago" -lt 3600 ]] && ! "$force"; then
      log "Last clean build was performed less than an hour ($last_build_time_sec_ago sec) ago"
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

mkdir -p "$BUILD_ROOT"
cd "$BUILD_ROOT"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$BUILD_ROOT/built_thirdparty"
if $clean_thirdparty; then
  log "Removing and re-building third-party dependencies (--clean-thirdparty specified)"
  (
    set -x
    cd "$YB_THIRDPARTY_DIR"
    git clean -dxf
    rm -f "$thirdparty_built_flag_file"
  )
fi

# Add the installed/bin directory to PATH so that we run the cmake binary from there.
export PATH="$YB_THIRDPARTY_DIR/installed/bin:$PATH"

if "$no_ccache"; then
  export YB_NO_CCACHE=1
fi

if "$no_tcmalloc"; then
  cmake_opts+=( -DYB_TCMALLOC_AVAILABLE=0 )
fi

if "$force_run_cmake" || [[ ! -f Makefile || ! -f $thirdparty_built_flag_file ]]; then
  if [ -f "$thirdparty_built_flag_file" ]; then
    log "$thirdparty_built_flag_file is present, setting NO_REBUILD_THIRDPARTY=1" \
      "before running cmake"
    export NO_REBUILD_THIRDPARTY=1
  fi
  log "Running cmake in $PWD"
  ( set -x; cmake "${cmake_opts[@]}" "$YB_SRC_ROOT" )
fi

if "$rocksdb_only"; then
  make_opts+=( build_rocksdb_all_targets )
fi

if [[ "${#object_files_to_delete[@]}" -gt 0 ]]; then
  log_empty_line
  log "Deleting object files corresponding to: ${object_files_to_delete[@]}"
  # TODO: can delete multiple files using the same find command.
  for object_file_to_delete in "${#object_files_to_delete[@]}"; do
    ( set -x; find "$BUILD_ROOT" -name "$object_files_to_delete" -exec rm -fv {} \; )
  done
  log_empty_line
fi

if [[ $cxx_test_name != "client_samples-test" ]]; then
  log "Running make in $PWD"
  set +u +e  # "set -u" may cause failures on empty lists
  time ( set -x; make -j8 "${make_opts[@]}" "${make_targets[@]}" )
  exit_code=$?
  set -u -e
  log "Non-java build finished with exit code $exit_code. Timing information is available above."
  if [ "$exit_code" -ne 0 ]; then
    exit "$exit_code"
  fi
  touch "$thirdparty_built_flag_file"
fi

if "$test_existence_check"; then
  (
    cd "$BUILD_ROOT"
    log "Checking if all test binaries referenced by CMakeLists.txt files exist."
    set +e
    YB_CHECK_TEST_EXISTENCE_ONLY=1 ctest -j8 2>&1 | grep Failed
    if [[ "${PIPESTATUS[0]}" -ne 0 ]]; then
      fatal "Some test binaries referenced in CMakeLists.txt files do not exist"
    fi
  )
fi

if [[ -n $cxx_test_name ]]; then
  set_asan_tsan_options
  cd "$BUILD_ROOT"
  ctest -R ^"$cxx_test_name"$ --output-on-failure
fi

# Check if the java build is needed. And skip java unit test runs if specified - time taken
# for tests is around two minutes currently.
if "$build_java"; then
  cd "$YB_SRC_ROOT"/java
  if $run_java_tests; then
    time ( build_yb_java_code install )
  else
    time ( build_yb_java_code install -DskipTests )
  fi
  log "Java build finished, total time information above."
fi
