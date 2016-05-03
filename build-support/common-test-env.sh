if [ "$BASH_SOURCE" == "$0" ]; then
  echo "$BASH_SOURCE must be sourced, not executed" >&2
  exit 1
fi

make_regex() {
  local regex=""
  for item in "$@"; do
    if [ -z "$item" ]; then
      continue
    fi
    if [ -n "$regex" ]; then
      regex+="|"
    fi
    regex+="$item"
  done
  echo "^($regex)$"
}

NON_GTEST_TESTS=(
  merge-test
  client_samples-test
  client_symbol-test
)
NON_GTEST_TESTS_RE=$( make_regex "${NON_GTEST_TESTS[@]}" )

NON_GTEST_ROCKSDB_TESTS=(
  c_test
  compact_on_deletion_collector_test
  db_sanity_test
  merge_test
  stringappend_test
)
NON_GTEST_ROCKSDB_TESTS_RE=$( make_regex "${NON_GTEST_ROCKSDB_TESTS[@]}" )

# Some tests are not based on the gtest framework and don't generate an XML output file.
# Also, RocksDB's thread_list_test is like that on Mac OS X.
function is_known_non_gtest_test() {
  local test_name=$1
  local is_rocksdb=$2

  if [[ ! "$is_rocksdb" =~ ^[01]$ ]]; then
    echo "The second argument to is_known_non_gtest_test (is_rocksdb) must be 0 or 1," \
         "was '$is_rocksdb'" >&2
    exit 1
  fi

  if ( [ "$is_rocksdb" == "1" ] &&
       ( [[ "$test_name" =~ $NON_GTEST_ROCKSDB_TESTS_RE ]] ||
         ( [ "$IS_MAC" == "1" ] && [ "$test_name" == thread_list_test ] ) ) || \
       ( [ "$is_rocksdb" == "0" ] && [[ "$test_name" =~ $NON_GTEST_TESTS_RE ]] ) ); then
    return 0  # true in bash
  else
    return 1  # false in bash
  fi
}

# This is used by yb_test.sh.
# Takes relative test name such as "bin/client-test" or "rocksdb-build/db_test".
function is_known_non_gtest_test_by_rel_path() {
  if [ $# -ne 1 ]; then
    echo "is_known_non_gtest_test_by_rel_path takes exactly one argument" \
         "(test executable path relative to the build directory)" >&2
    exit 1
  fi
  local rel_test_path=$1
  if [[ ! "$rel_test_path" =~ ^(bin|rocksdb-build)/[^/]+$ ]]; then
    echo "Invalid format of the test executable, expected to start with" \
         "'bin/' or 'rocksdb-build/': $rel_test_path" >&2
    exit 1
  fi
  local test_binary_basename=$( basename "$test_binary" )
  # Remove .sh extensions for bash-based tests.
  local test_binary_basename_no_ext=${test_binary_basename%[.]sh}
  local is_rocksdb=0
  if [[ "$rel_test_path" =~ ^rocksdb-build/ ]]; then
    is_rocksdb=1
  fi

  is_known_non_gtest_test "$test_binary_basename_no_ext" "$is_rocksdb"
}


if [ -z "$BUILD_ROOT" ]; then
  echo "The BUILD_ROOT environment variable is not set" >&2
  exit 1
fi

if [ ! -d "$BUILD_ROOT" ]; then
  echo "The directory '$BUILD_ROOT' does not exist" >&2
  exit 1
fi

# Absolute path to the root source directory. This script is expected to be inside the build-support
# subdirectory of the source directory.
SOURCE_ROOT=$(cd "$(dirname "$BASH_SOURCE")"/.. && pwd)
if [ ! -d "$SOURCE_ROOT/build" ]; then
  echo "Could not determine source root directory from script path '$BASH_SOURCE'." \
    "The auto-detected directory '$SOURCE_ROOT' does not contain a 'build' directory." >&2
  exit 1
fi

common_dynamic_lib_dirs="$SOURCE_ROOT/thirdparty/installed-deps/lib"
common_dynamic_lib_dirs+=":$SOURCE_ROOT/thirdparty/installed/lib"
if [ "$(uname)" == "Darwin" ]; then
  IS_MAC=1
  export DYLD_FALLBACK_LIBRARY_PATH="$BUILD_ROOT/rocksdb-build:$common_dynamic_lib_dirs"
  STACK_TRACE_FILTER=cat
else
  IS_MAC=0
  export LD_LIBRARY_PATH="$common_dynamic_lib_dirs"
fi

unset common_dynamic_lib_dirs
