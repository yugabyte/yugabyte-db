# Copyright (c) YugaByte, Inc.

# git revision of google style guide:
# https://github.com/google/styleguide
# git archive --prefix=google-styleguide-$(git rev-parse HEAD)/ \
#              -o /tmp/google-styleguide-$(git rev-parse HEAD).tgz HEAD
GSG_VERSION=7a179d1ac2e08a5cc1622bec900d1e0452776713
GSG_DIR=$TP_SOURCE_DIR/google-styleguide-${GSG_VERSION}

build_cpplint() {
  (
    set_build_env_vars

    # Copy cpplint tool into bin directory
    set -x
    cp -f "$GSG_DIR"/cpplint/cpplint.py "$PREFIX"/bin/cpplint.py
  )
}
