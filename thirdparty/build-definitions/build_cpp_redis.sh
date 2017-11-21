# Copyright (c) YugaByte, Inc.

CPP_REDIS_VERSION=4.3.0
CPP_REDIS_DIR=$TP_SOURCE_DIR/cpp_redis-${CPP_REDIS_VERSION}

CPP_REDIS_URL=\
"https://github.com/Cylix/cpp_redis/archive/${CPP_REDIS_VERSION}.zip"

TP_NAME_TO_SRC_DIR["cpp_redis"]=$CPP_REDIS_DIR
TP_NAME_TO_ARCHIVE_NAME["cpp_redis"]="cpp_redis-${CPP_REDIS_VERSION}.zip"
TP_NAME_TO_URL["cpp_redis"]="$CPP_REDIS_URL"

build_cpp_redis() {
  # Configure for a very minimal install - basically only HTTP, since we only
  # use this for testing our own HTTP endpoints at this point in time.
  create_build_dir_and_prepare "$CPP_REDIS_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x

    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX/cpp_redis \
          -DCMAKE_PREFIX_PATH=$PREFIX
    run_make
    make install
    mv $PREFIX/cpp_redis/lib/lib/*.a $PREFIX/lib/
    rm -rf $PREFIX/include/cpp_redis
    mv $PREFIX/cpp_redis/include/* $PREFIX/include/
  )
}
