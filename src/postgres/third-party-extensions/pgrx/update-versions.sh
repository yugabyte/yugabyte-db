#! /usr/bin/env bash
#LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
#LICENSE
#LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
#LICENSE
#LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
#LICENSE
#LICENSE All rights reserved.
#LICENSE
#LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
 

# requires:
# * pgrx-version-updater (no intervention required -- built on demand from this project)
#
# To run this with more output, set environment variable VERBOSE to either 1 or true. E.g.
#   $ VERBOSE=1 ./update-versions.sh 1.6.0

if [ "$1" == "" ]; then
  echo "usage:  ./update-versions.sh <VERSION>"
  exit 1
fi

set -e

VERSION=$1

if [ "$VERBOSE" == "1" ] || [ "$VERBOSE" == "true" ]; then
  echo "Verbose output requested."
  VERBOSE=1
  set -x
else
  CARGO_QUIET_FLAG=-q
  unset VERBOSE
fi

# INCLUDE_FOR_DEP_UPDATES specifies an array of relative paths that point to Cargo
# TOML files that are not automatically found by the pgrx-version-updater tool but
# still have PGRX dependencies that require updating.
INCLUDE_FOR_DEP_UPDATES=(
  'cargo-pgrx/src/templates/cargo_toml'
)

# EXCLUDE_FROM_VERSION_BUMP specifies an array of relative paths that point to
# Cargo TOML files that should be *excluded* from package version bumps. Also used
# below to include all pgrx-example Cargo TOML files since they do not get their
# versions bumped at release time.
EXCLUDE_FROM_VERSION_BUMP=(
  'cargo-pgrx/src/templates/cargo_toml'
  'tools/version-updater/Cargo.toml'
)

# Exclude all pgrx-examples Cargo.toml files from version bumping
for file in pgrx-examples/**/Cargo.toml; do
  EXCLUDE_FROM_VERSION_BUMP+=("$file")
done

# shellcheck disable=SC2086,SC2068
cargo run --manifest-path=./tools/version-updater/Cargo.toml \
  update-files \
  --update-version "$VERSION" \
  ${INCLUDE_FOR_DEP_UPDATES[@]/#/-i } \
  ${EXCLUDE_FROM_VERSION_BUMP[@]/#/-e } \
  ${VERBOSE:+--show-diff} \
  ${VERBOSE:+--verbose}


echo "Generating bindings -- this may take a few moments"
PGRX_PG_SYS_GENERATE_BINDINGS_FOR_RELEASE=1 cargo test --no-run $CARGO_QUIET_FLAG --workspace --no-default-features --features "pg${PG_VER:-14}"

echo "Done!"
