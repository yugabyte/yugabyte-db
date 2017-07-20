# Copyright (c) YugaByte, Inc.

CRYPT_BLOWFISH_VERSION=79ee670d1a2977328c481d6578387928ff92896a
CRYPT_BLOWFISH_DIR=$TP_SOURCE_DIR/crypt_blowfish-$CRYPT_BLOWFISH_VERSION
CRYPT_BLOWFISH_ARCHIVE=crypt_blowfish-${CRYPT_BLOWFISH_VERSION}.tar.gz
TP_NAME_TO_SRC_DIR["crypt_blowfish"]=$CRYPT_BLOWFISH_DIR
CRYPT_BLOWFISH_URL="https://github.com/YugaByte/crypt_blowfish/archive/${CRYPT_BLOWFISH_VERSION}.tar.gz"

build_crypt_blowfish() {
  create_build_dir_and_prepare "$CRYPT_BLOWFISH_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    run_make clean
    run_make
    # Make top-level dirs for this library.
    local crypt_blowfish_include_dir="$PREFIX/include/crypt_blowfish"
    mkdir -p "$crypt_blowfish_include_dir"
    # Copy over all the headers into a generic include/ directory.
    ls | grep -E ".h$" | xargs -I{} rsync -av "{}" "$crypt_blowfish_include_dir"
    # Generate and copy the lib into a generic lib/ directory.
    ar r libcrypt_blowfish.a *.o
    cp libcrypt_blowfish.a "$PREFIX/lib/"
  )
}
