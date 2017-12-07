# Copyright (c) YugaByte, Inc.

GMOCK_VERSION=1.8.0
GMOCK_DIR=$TP_SOURCE_DIR/googletest-release-$GMOCK_VERSION
GMOCK_URL="https://github.com/google/googletest/archive/release-${GMOCK_VERSION}.tar.gz"
TP_NAME_TO_SRC_DIR["gmock"]=$GMOCK_DIR
TP_NAME_TO_URL["gmock"]=$GMOCK_URL
TP_NAME_TO_ARCHIVE_NAME["gmock"]="gmock-${GMOCK_VERSION}.tar.gz"

build_gmock() {
  create_build_dir_and_prepare "$GMOCK_DIR"
  local shared
  local build_dir=$PWD
  log "Build directory for gmock to be copied to separate static/shared build dirs: $build_dir"
  for shared in OFF ON; do
    local cur_build_dir=$build_dir
    if [[ $shared == "ON" ]]; then
      cur_build_dir+="_shared"
    else
      cur_build_dir+="_static"
    fi
    ( set -x; mkdir -p "$cur_build_dir" )
    log "Building gmock with shared libraries turned $shared using build directory $cur_build_dir"
    (
      cd "$cur_build_dir"
      remove_cmake_cache
      (
        set_build_env_vars
        set_thirdparty_flags_for_cmake_projects
        YB_REMOTE_BUILD=0 cmake \
          -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_POSITION_INDEPENDENT_CODE=On \
          -DBUILD_SHARED_LIBS=$shared \
          "$GMOCK_DIR"
        run_make
      )

      log "Installing gmock (shared=$shared)"
      (
        set_build_env_vars
        if [[ $shared == "OFF" ]]; then
          cp -a googlemock/libgmock.a "$PREFIX/lib/"
        else
          cp -a "googlemock/libgmock.$DYLIB_SUFFIX" "$PREFIX/lib/"
          rsync -av "$GMOCK_DIR/googlemock/include/" "$PREFIX/include/"
          rsync -av "$GMOCK_DIR/googletest/include/" "$PREFIX/include/"
        fi
      )
    )
  done
}
