#!/usr/bin/env bash
set -euo pipefail

# Source the common-build-env.sh script in the build-support subdirectory of the directory of this
# script.
. "${0%/*}"/build-support/common-build-env.sh

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} [<options>] [<build_type>]
Options:
  -h, --help
    Show help
  --verbose
    Show debug output from CMake
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
  --skip-java-build
    Do not package and install java source code.
  --run-java-tests
    Run the java unit tests when build is enabled.

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

while [ $# -gt 0 ]; do
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
    --skip-java-build)
      build_java=false
    ;;
    --run-java-tests)
      run_java_tests=true
    ;;
    debug|fastdebug|release|profile_gen|profile_build|asan|tsan)
      build_type="$1"
      build_type_specified=true
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

if [[ "$OSTYPE" =~ ^darwin && "$YB_COMPILER_TYPE" != "clang" ]]; then
  echo "Can only build with clang on Mac OS X, but found YB_COMPILER_TYPE=$YB_COMPILER_TYPE" >&2
  exit 1
fi

cmake_opts=()
case "$build_type" in
  asan)
    cmake_opts+=( -DYB_USE_ASAN=1 -DYB_USE_UBSAN=1 )
    cmake_build_type=fastdebug
    if [ -n "$YB_COMPILER_TYPE" ] && [ "$YB_COMPILER_TYPE" != "clang" ]; then
      echo "ASAN builds require clang"
    fi
    YB_COMPILER_TYPE="clang"
  ;;
  tsan)
    echo "TSAN builds are not supported yet" >&2
    exit 1
  ;;
  *)
    cmake_build_type=$build_type
esac

set_build_root "$build_type"

# We have set cmake_build_type and BUILD_ROOT based on build_type, so build_type is not needed
# anymore. The difference between build_type and cmake_build_type is that the former can be set to
# some special values, such as "tsan" and "asan".
unset build_type

validate_cmake_build_type "$cmake_build_type"

export YB_COMPILER_TYPE

cmake_opts=( "-DCMAKE_BUILD_TYPE=$cmake_build_type" )

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
      echo "Last clean build was performed less than an hour ($last_build_time_sec_ago sec) ago" >&2
      echo "Do you still want to do a clean build? [y/N]" >&2
      read answer
      if [[ ! "$answer" =~ ^[yY]$ ]]; then
        echo "Operation canceled" >&2
        exit 1
      fi
    fi
  fi
  mkdir -p "$YB_SRC_ROOT/build"
  echo "$current_timestamp_sec" >"$last_clean_timestamp_path"
fi

if "$clean_before_build"; then
  echo "Removing '$BUILD_ROOT' (--clean specified)"
  ( set -x; rm -rf "$BUILD_ROOT" )
fi

mkdir -p "$BUILD_ROOT"
cd "$BUILD_ROOT"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$BUILD_ROOT/built_thirdparty"
if $clean_thirdparty; then
  echo "Removing and re-building third-party dependencies (--clean-thirdparty specified)"
  (
    set -x
    cd "$thirdparty_dir"
    git clean -dxf
    rm -f "$thirdparty_built_flag_file"
  )
fi

# Add the installed/bin directory to PATH so that we run the cmake binary from there.
export PATH="$thirdparty_dir/installed/bin:$PATH"

if "$no_ccache"; then
  cmake_opts+=( -DYB_NO_CCACHE=1 )
fi

if "$force_run_cmake" || [ ! -f Makefile ] || [ ! -f "$thirdparty_built_flag_file" ]; then
  if [ -f "$thirdparty_built_flag_file" ]; then
    echo "$thirdparty_built_flag_file is present, setting NO_REBUILD_THIRDPARTY=1" \
      "before running cmake"
    export NO_REBUILD_THIRDPARTY=1
  fi
  echo "Running cmake in $PWD"
  ( set -x; cmake -DYB_LINK=dynamic "${cmake_opts[@]}" "$YB_SRC_ROOT" )
fi

if "$rocksdb_only"; then
  make_opts+=( build_rocksdb_all_targets )
fi

echo Running make in $PWD
set +u +e  # "set -u" may cause failures on empty lists
time ( set -x; make -j8 "${make_opts[@]}" )
exit_code=$?
set -u -e
echo "Non-java build finished with exit code $exit_code. Timing information is available above."
if [ "$exit_code" -ne 0 ]; then
  exit "$exit_code"
fi

touch "$thirdparty_built_flag_file"

"$YB_SRC_ROOT"/build-support/fix_rpath.py --build-root "$BUILD_ROOT"

# Check if the java build is needed. And skip java unit test runs if specified - time taken
# for tests is around two minutes currently.
if $build_java; then
  cd "$YB_SRC_ROOT"/java
  if command -v mvn 2>/dev/null; then
    if $run_java_tests; then
      time ( mvn install )
    else
      time ( mvn install -DskipTests )
    fi
    echo "Java build finished, total time information above."
  else
    echo "NOTE: mvn binary not found, skipping java build."
  fi
fi
