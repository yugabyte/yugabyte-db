# Copyright (c) YugaByte, Inc.

build_boost_uuid() {
  # Copy boost_uuid into the include directory.
  # This is a header-only library which isn't present in some older versions of
  # boost (eg the one on el6). So, we check it in and put it in our own include
  # directory.
  (
    set_build_env_vars
    set -x
    rsync -a "$TP_DIR"/boost_uuid/boost/ "$PREFIX"/include/boost/
  )
}
