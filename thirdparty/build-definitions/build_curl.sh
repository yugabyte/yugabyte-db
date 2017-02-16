# Copyright (c) YugaByte, Inc.

CURL_VERSION=7.32.0
CURL_DIR=$TP_SOURCE_DIR/curl-${CURL_VERSION}

build_curl() {
  # Configure for a very minimal install - basically only HTTP, since we only
  # use this for testing our own HTTP endpoints at this point in time.
  create_build_dir_and_prepare "$CURL_DIR"
  (
    set_build_env_vars
    set -x
    ./configure --prefix=$PREFIX \
      --disable-ftp \
      --disable-file \
      --disable-ldap \
      --disable-ldaps \
      --disable-rtsp \
      --disable-dict \
      --disable-telnet \
      --disable-tftp \
      --disable-pop3 \
      --disable-imap \
      --disable-smtp \
      --disable-gopher \
      --disable-manual \
      --without-librtmp \
      --disable-ipv6
    run_make
    make install
  )
}
