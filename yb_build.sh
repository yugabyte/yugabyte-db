#!/bin/bash 

set -euo pipefail

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

Build types:
  debug (default), fastdebug, release, profile_gen, profile_build
EOT
}

cmake_build_type="debug"
verbose=false
force_run_cmake=false
clean_before_build=false
clean_thirdparty=false

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
    debug|fastdebug|release|profile_gen|profile_build)
      clean_before_build="$1"
    ;;
    *)
      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

cmake_opts=( "-DCMAKE_BUILD_TYPE=$cmake_build_type" )
make_opts=""

project_dir=$( cd "$( dirname $0 )" && pwd )
build_dir="$project_dir/build/$cmake_build_type"

if $verbose; then 
  # http://stackoverflow.com/questions/22803607/debugging-cmakelists-txt
  cmake_opts+=( -Wdev --debug-output --trace )
  make_opts+=" VERBOSE=1"
fi

if $clean_before_build; then
  echo "Removing '$build_dir' (--clean specified)"
  ( set -x; rm -rf "$build_dir" )
fi

mkdir -p "$build_dir"
cd "$build_dir"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$build_dir/built_thirdparty"
if $clean_thirdparty; then
  echo "Removing and re-building third-party dependencies (--clean-thirdparty specified)"
  (
    set -x
    cd "$project_dir/thirdparty"
    git clean -dxf
    rm -f "$thirdparty_built_flag_file"
  )
fi

if $force_run_cmake || [ ! -f Makefile ] || [ ! -f "$thirdparty_built_flag_file" ]; then
  if [ -f "$thirdparty_built_flag_file" ]; then
    echo "$thirdparty_built_flag_file is present, setting NO_REBUILD_THIRDPARTY=1" \
      "before running cmake"
    export NO_REBUILD_THIRDPARTY=1
  fi
  echo "Running cmake in $PWD"
  ( set -x; cmake -DYB_LINK=dynamic "${cmake_opts[@]}" "$project_dir" )
fi

echo Running make in $PWD
( set -x; make -j8 $make_opts )

touch "$thirdparty_built_flag_file"

