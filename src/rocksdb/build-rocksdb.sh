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
EOT
}

build_dir=""
build_type=""
make_parallelism=""
debug_level=""
link_mode="d"

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

make_targets=( tools benchmarks )
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

if [ "$debug_level" -gt 0 ]; then
  # We can only build tests if NDEBUG is not defined (otherwise e.g. db_test.cc fails to build).
  make_targets+=( tests )
fi

# Normalize the directory in case it contains relative components ("..").
build_dir=$( cd "$build_dir" && pwd )

if [ -z "$build_type" ]; then
  build_type=${build_dir##*/}
fi

make_opts=""
if [ -n "$make_parallelism" ]; then
  if [[ ! "$make_parallelism" =~ ^[0-9]+$ ]]; then
    echo "Invalid value for --make-parallelism: '$make_parallelism'" >&2
    exit 1
  fi
  make_opts="-j$make_parallelism"
fi

if [ -n "$debug_level" ]; then
  if [[ ! "$debug_level" =~ ^[0-2] ]]; then
    echo "Invalid value for --debug-level: must be 0, 1, or 2" >&2
    exit 1
  fi
  make_opts+=" DEBUG_LEVEL=$debug_level"
fi

build_type_lowercase=$( echo "$build_type" | tr '[:upper:]' '[:lower:]' )
if [ "${build_dir##*/}" != "$build_type_lowercase" ]; then
  echo "Build directory '$build_dir' does not end with build type ('$build_type') as its last" \
    "path component: '$build_dir'" >&2
  exit 1
fi

rocksdb_dir=$( cd "`dirname $0`" && pwd )

if [ ! -f "$rocksdb_dir/Makefile" ]; then
  echo "Makefile not found in RocksDB source directory '$rocksdb_dir'" >&2
  exit 1
fi

link_dir="$build_dir/rocksdb-symlinks-only"
build_dir="$build_dir/rocksdb-build"

set -x

rm -rf "$link_dir"

cd "$rocksdb_dir"
make clean  # ensure there are no build artifacts in the RocksDB source directory itself

CP=cp
if [ "`uname`" == "Darwin" ]; then
  # The default cp command on Mac OS X does not support the "-s" argument (mirroring a directory
  # using a tree of symlinks). We install the "gcp" ("g" for GNU) from using
  # "brew install coreutils".
  CP=gcp
fi

# Create a "link-only" directory inside of our build directory that mirrors the RocksDB source tree.
$CP -Rs "$rocksdb_dir" "$link_dir"

# Sync any new links that may have been created into our RocksDB build directory that may already
# contain build artifacts from an earlier build.
rsync -al "$link_dir/" "$build_dir"

cd "$build_dir"

set -x
make $make_opts ${make_targets[@]}
