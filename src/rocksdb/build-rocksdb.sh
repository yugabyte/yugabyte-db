#!/bin/bash

# Copyright (c) YugaByte, Inc.

set -euo pipefail

print_help() {
  echo <<-EOT
Usage: ${0##*/} <options>
Links RocksDB sources into the build directory and builds it.
Options:
  -h, --help
    Show help.
  --build-type <build_type>
    The build type (e.g. "debug" or "release"). This must match the build directory.
    This is optional. If specified, this is expected to match the final component of the build
    directory.
  --build-dir <build_dir>
    The base build directory, e.g. yugabyte/build/debug or yugabyte/build/release.
    This is required.
  --make-parallelism <parallelism>
    Specify the parallelism of the make command. This is frequently the number of CPUs
    or hyper-threads supported by the host.
  --debug-level <debug_level>
    The debug level to build RocksDB with (0, 1, or 2). See src/rocksdb/Makefile for details.
  --link-mode <link_mode>
    This is "s" for static linking or "d" for dynamic linking (default).
  --cxx-compiler <c++_compiler_path>
  --c-compiler <c_compiler_path>
    These options specify C++ and C compilers to use.
  --use-ld-gold
    Specify to use the ld.gold linker.
  --build-all-targets
    Specify to build tests, tools, and benchmarks
  --target <target>
    Explicitly specify an additional Makefile target to build. This option can be repeated and all
    targets specified this way will be built.
  --skip-link-dir-creation
    Skip creation of a symlink directory tree for an out-of-source RocksDB build. This can be used
    if we know this has already been done (e.g. when building a test right after building the
    RocksDB library).
EOT
}

build_dir=""
build_type=""
make_parallelism=""
debug_level=""
link_mode="d"
cxx_compiler=""
c_compiler=""
use_ld_gold=false
extra_ldflags=""
verbose=false
build_all_targets=false
skip_link_dir_creation=false

make_opts=()
make_targets=()

while [ $# -ne 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    --build-type)
      build_type=$2
      shift
    ;;
    --build-dir)
      build_dir=$2
      shift
    ;;
    --make-parallelism)
      make_parallelism=$2
      shift
    ;;
    --debug-level)
      debug_level=$2
      shift
    ;;
    --link-mode)
      link_mode=$2
      shift
    ;;
    --cxx-compiler)
      cxx_compiler=$2
      shift
    ;;
    --c-compiler)
      c_compiler=$2
      shift
    ;;
    --use-ld-gold)
      use_ld_gold=true
    ;;
    --verbose)
      verbose=true
    ;;
    --build-tests)
      build_tests=true
    ;;
    --build-all-targets)
      build_all_targets=true
    ;;
    --target)
      make_targets+=( "$2" )
      shift
    ;;
    --skip-link-dir-creation)
      skip_link_dir_creation=true
    ;;
    *)
      print_help >&2
      echo >&2
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

if [ -z "$build_dir" ]; then
  echo "--build-dir is not specified"
fi

if [ ! -d "$build_dir" ]; then
  echo "Directory specified by --build-dir ('$build_dir') does not exist or is not a directory" >&2
  exit 1
fi

if [ -n "$cxx_compiler" ]; then
  if [ ! -f "$cxx_compiler" ]; then
    echo "C++ compiler does not exist at the location specified with --cxx-compiler:" \
         "'$cxx_compiler'" >&2
    exit 1
  fi
  make_opts+=( CXX="$cxx_compiler" )
fi

if [ -n "$c_compiler" ]; then
  if [ ! -f "$c_compiler" ]; then
    echo "C compiler does not exist at the location specified with --c-compiler:" \
         "'$c_compiler'" >&2
    exit 1
  fi
  make_opts+=( CC="$c_compiler" )
fi

if $verbose; then
  make_opts+=( SH="bash -x" )
fi

if $use_ld_gold; then
  # TODO: replace this with an append if we're accumulating linker flags in multiple places.
  extra_ldflags="-fuse-ld=gold"
fi

case "$link_mode" in
  d)
    make_targets+=( shared_lib )
  ;;
  s)
    make_targets+=( static_lib )
  ;;
  *)
    echo "Invalid '$link_mode' value for --link-mode:" \
      "must either be 'd' (dynamic) or 's' (static)" >&2
    exit 1
esac

if [ "$debug_level" -gt 0 ] && $build_all_targets; then
  # We can only build tests if NDEBUG is not defined (otherwise e.g. db_test.cc fails to build).
  make_targets+=( tests )
fi

if $build_all_targets; then
  make_targets+=( tools benchmarks )
fi

# Normalize the directory in case it contains relative components ("..").
build_dir=$( cd "$build_dir" && pwd )

if [ -z "$build_type" ]; then
  build_type=${build_dir##*/}
fi

if [ -n "$make_parallelism" ]; then
  if [[ ! "$make_parallelism" =~ ^[0-9]+$ ]]; then
    echo "Invalid value for --make-parallelism: '$make_parallelism'" >&2
    exit 1
  fi
  make_opts+=( "-j$make_parallelism" )
fi

if [ -n "$debug_level" ]; then
  if [[ ! "$debug_level" =~ ^[0-2] ]]; then
    echo "Invalid value for --debug-level: must be 0, 1, or 2" >&2
    exit 1
  fi
  make_opts+=( "DEBUG_LEVEL=$debug_level" )
fi

build_type_lowercase=$( echo "$build_type" | tr '[:upper:]' '[:lower:]' )
build_dir_basename=$( echo "${build_dir##*/}" | tr '[:upper:]' '[:lower:]' )

# If the build type is "debug", we check that the directory ends with "debug" or "debug0"
# (case-insensitive). We need the "debug0" case because we sometimes get paths such as
# /home/mbautin/.CLion12/system/cmake/generated/411cc071/411cc071/Debug0 in CLion builds.
if [ "$build_dir_basename" != "$build_type_lowercase" ] && \
   [ "$build_dir_basename" != "${build_type_lowercase}0" ]; then
  echo "Build directory '$build_dir' does not end with build type ('$build_type') optionally " \
       "followed by 0 as its final path component" >&2
  exit 1
fi

echo "RocksDB build type: $build_type_lowercase"
echo "Base build directory for RocksDB: $build_dir"

rocksdb_dir=$( cd "`dirname $0`" && pwd )

if [ ! -f "$rocksdb_dir/Makefile" ]; then
  echo "Makefile not found in RocksDB source directory '$rocksdb_dir'" >&2
  exit 1
fi

link_dir="$build_dir/rocksdb-symlinks-only"
rocksdb_build_dir="$build_dir/rocksdb-build"

CP=cp
if [ "`uname`" == "Darwin" ]; then
  # The default cp command on Mac OS X does not support the "-s" argument (mirroring a directory
  # using a tree of symlinks). We install the "gcp" ("g" for GNU) from using
  # "brew install coreutils".
  CP=gcp
fi

if [ -n "$extra_ldflags" ]; then
  make_opts+=( EXTRA_LDFLAGS="$extra_ldflags" )
fi

# -------------------------------------------------------------------------------------------------
# Every command will be logged from this point on. All variables should have been set up above.

set -x

if ! $skip_link_dir_creation; then
  rm -rf "$link_dir"

  cd "$rocksdb_dir"
  make clean  # ensure there are no build artifacts in the RocksDB source directory itself

  # Create a "link-only" directory inside of our build directory that mirrors the RocksDB source tree.
  $CP -Rs "$rocksdb_dir" "$link_dir"

  # Sync any new links that may have been created into our RocksDB build directory that may already
  # contain build artifacts from an earlier build.
  rsync -al "$link_dir/" "$rocksdb_build_dir"
fi

mkdir -p "$rocksdb_build_dir"
cd "$rocksdb_build_dir"

set +u  # make_opts may be empty and that we don't want that to be treated as an undefined variable
make "${make_opts[@]}" "${make_targets[@]}"

set +x -u

# Now we're handling a weird issue that only happens during CLion-triggered builds.
# The RocksDB library is somehow expected to exist in __default__/rocksdb-build instead of
# e.g. Debug/rocksdb-build, so we just symlink it there.

if [[ "$build_dir" =~ /[.]CLion.*/ ]]; then
  if [ -d "$build_dir/../__default__" ]; then
    mkdir -p "$build_dir/../__default__/rocksdb-build"
    for rocksdb_lib_path in "$rocksdb_build_dir"/librocksdb*; do
      if [ -f "$rocksdb_lib_path" ]; then
        dest_path="$build_dir/../__default__/rocksdb-build/${rocksdb_lib_path##*/}"
        if [ ! -f "$dest_path" ]; then
          echo "Creating a symlink '$dest_path' to '${rocksdb_lib_path}' for CLion"
          ln -s "$rocksdb_lib_path" "$dest_path"
        fi
      fi
    done
  fi
fi
