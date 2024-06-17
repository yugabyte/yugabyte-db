export BUILD_ROOT=~/code/yugabyte-db/build/debug-clang-dynamic-arm64-ninja/;
export YB_BUILD_ROOT=~/code/yugabyte-db/build/debug-clang-dynamic-arm64-ninja/;
export YB_SRC_ROOT=~/code/yugabyte-db/;
make clean PG_CONFIG=$BUILD_ROOT/postgres/bin/pg_config USE_PGXS=1
