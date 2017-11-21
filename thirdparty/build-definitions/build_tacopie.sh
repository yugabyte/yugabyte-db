# Copyright (c) YugaByte, Inc.

TACOPIE_VERSION=3.2.0
TACOPIE_DIR=$TP_SOURCE_DIR/tacopie-${TACOPIE_VERSION}

TACOPIE_URL=\
"https://github.com/Cylix/tacopie/archive/${TACOPIE_VERSION}.zip"

TP_NAME_TO_SRC_DIR["tacopie"]=$TACOPIE_DIR
TP_NAME_TO_ARCHIVE_NAME["tacopie"]="tacopie-${TACOPIE_VERSION}.zip"
TP_NAME_TO_URL["tacopie"]="$TACOPIE_URL"

build_tacopie() {
  # Configure for a very minimal install - basically only HTTP, since we only
  # use this for testing our own HTTP endpoints at this point in time.
  create_build_dir_and_prepare "$TACOPIE_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x

    echo "Build tacopie with $PREFIX"
    cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$PREFIX"
    run_make
    make install
    find "$PREFIX" -name *tacopie*
  )
}
