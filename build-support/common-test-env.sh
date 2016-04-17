if [ -z "$BUILD_ROOT" ]; then
  echo "The BUILD_ROOT environment variable is not set" >&2
  exit 1
fi

if [ ! -d "$BUILD_ROOT" ]; then
  echo "The directory '$BUILD_ROOT' does not exist" >&2
  exit 1
fi

# Absolute path to the root source directory. This script is expected to live within it.
SOURCE_ROOT=$(cd "$(dirname "$BASH_SOURCE")"/.. && pwd)
if [ ! -d "$SOURCE_ROOT/build" ]; then
  echo "Could not determine source root directory from script path '$BASH_SOURCE'." \
    "The auto-detected directory '$SOURCE_ROOT' does not contain a 'build' directory." >&2
  exit 1
fi

common_dynamic_lib_dir="$SOURCE_ROOT/thirdparty/installed-deps/lib"
if [ "$(uname)" == "Darwin" ]; then
  export DYLD_FALLBACK_LIBRARY_PATH="$BUILD_ROOT/rocksdb-build:$common_dynamic_lib_dir"
  STACK_TRACE_FILTER=cat
else
  export LD_LIBRARY_PATH="$common_dynamic_lib_dir"
fi

unset common_dynamic_lib_dir