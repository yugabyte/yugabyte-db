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
  --cxx-flags <flags>
    A list of additional CXX flags to use when building.
  --use-ld-gold
    Specify to use the ld.gold linker.
  --targets <targets>
    Explicitly specify additional Makefile targets to build. <targets> can be one target or a
    whitespace-spearated list of targets. This option can be repeated and all targets specified this
    way will be built. A special-case target "all" is replaced with our custom "complete set" of
    targets (tests, tools, etc.)
  --skip-link-dir-creation
    Skip creation of a symlink directory tree for an out-of-source RocksDB build. This can be used
    if we know this has already been done (e.g. when building a test right after building the
    RocksDB library).
EOT
}

script_dir=$( cd "$( dirname "$0" )" && pwd )
YB_SRC_ROOT=$( cd "$script_dir"/../.. && pwd )
if [[ ! -d "$YB_SRC_ROOT/thirdparty" ]]; then
  echo "Could not determine yugabyte source root: no 'thirdparty' subdirectory found in" \
       "'$YB_SRC_ROOT'" >&2
  exit 1
fi

build_dir=""
c_compiler=""
cxx_compiler=""
cxx_flags=""
debug_level=""
extra_include_dirs=()
extra_lib_dirs=()
link_mode="d"
make_parallelism=""
skip_link_dir_creation=false
use_ld_gold=false
verbose=false

make_opts=( NO_DEBUG_LEVEL_OVERRIDE=1 )
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
    --cxx-flags)
      cxx_flags=$2
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
    --targets)
      make_targets+=( $2 )
      shift
    ;;
    --skip-link-dir-creation)
      skip_link_dir_creation=true
    ;;
    --extra-include-dir)
      extra_include_dirs+=( "$2" )
      shift
    ;;
    --extra-lib-dir)
      extra_lib_dirs+=( "$2" )
      shift
    ;;
    *)
      print_help >&2
      echo >&2
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

extra_cxxflags=""
extra_ldflags=""

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

if "$verbose"; then
  make_opts+=( SH="bash -x" )
fi

if "$use_ld_gold"; then
  # TODO: replace this with an append if we're accumulating linker flags in multiple places.
  extra_ldflags+=" -fuse-ld=gold"
fi

set +u
for extra_include_dir in "${extra_include_dirs[@]}"; do
  if [ ! -d "$extra_include_dir" ]; then
    echo "Extra include directory '$extra_include_dir' does not exist" >&2
    exit 1
  fi
  extra_cxxflags+=" -I'$extra_include_dir'"
done
# Add the common CXX flags to the extra flags variable.
extra_cxxflags+=" $cxx_flags"
for extra_lib_dir in "${extra_lib_dirs[@]}"; do
  if [ ! -d "$extra_lib_dir" ]; then
    echo "Extra library directory '$extra_lib_dir' does not exist" >&2
    exit 1
  fi
  extra_ldflags+=" -L'$extra_lib_dir'"
done

set -u

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

new_make_targets=()
build_all_targets=false
for target in "${make_targets[@]}"; do
  if [ "$target" == "all" ]; then
    build_all_targets=true
  else
    new_make_targets+=( "$target" )
  fi
done

if "$build_all_targets"; then
  # Build tests.
  # env_mirror_test does not seem to be built as part of the tests target, so we add it explicitly.
  new_make_targets+=(
    benchmarks
    env_mirror_test
    tests
    tools
  )
fi

# Sort / deduplicate the targets
IFS=$'\n'
make_targets=( $( for t in "${new_make_targets[@]}"; do echo "$t"; done | sort | uniq ) )
unset IFS

# Normalize the directory in case it contains relative components ("..").
build_dir=$( cd "$build_dir" && pwd )

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

if [ -n "$extra_cxxflags" ]; then
  make_opts+=( EXTRA_CXXFLAGS="$extra_cxxflags" )
fi

# TODO: this should probably be installed-deps-tsan in some cases.
make_opts+=( EXEC_LDFLAGS="-Wl,-rpath,$YB_SRC_ROOT/thirdparty/installed-deps/lib" )

if ! $skip_link_dir_creation; then
  (
    set -x
    rm -rf "$link_dir"

    cd "$rocksdb_dir"

    make clean  # ensure there are no build artifacts in the RocksDB source directory itself

    # Create a "link-only" directory inside of our build directory that mirrors the RocksDB source tree.
    $CP -Rs "$rocksdb_dir" "$link_dir"

    # Sync any new links that may have been created into our RocksDB build directory that may already
    # contain build artifacts from an earlier build.
    rsync -al "$link_dir/" "$rocksdb_build_dir"

  )
fi

( set -x; mkdir -p "$rocksdb_build_dir" )
cd "$rocksdb_build_dir"

set +u  # make_opts may be empty and we don't want that to be treated as an undefined variable.

( set -x; make "${make_opts[@]}" "${make_targets[@]}" )

set -u

# Now we're handling a weird issue that only happens during CLion-triggered builds.
# The RocksDB library and test binaries are expected to exist in __default__/rocksdb-build instead
# of e.g. Debug/rocksdb-build, so we just symlink them there.

if [[ "$build_dir" =~ /[.]CLion.*/ ]]; then
  if [ -d "$build_dir/../__default__" ]; then
    ( set -x; mkdir -p "$build_dir/../__default__/rocksdb-build" )
    for symlink_target in "$rocksdb_build_dir"/librocksdb* "$rocksdb_build_dir"/*_test; do
      if [ -f "$symlink_target" ]; then
        dest_path="$build_dir/../__default__/rocksdb-build/${symlink_target##*/}"
        if [ ! -f "$dest_path" ]; then
          echo "Creating a symlink '$dest_path' to '${symlink_target}' for CLion"
          ( set -x; ln -s "$symlink_target" "$dest_path" )
        fi
      fi
    done
  fi
fi
