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
 

# requires:  "cargo install cargo-edit" from https://github.com/killercup/cargo-edit
EXCLUSIONS="--exclude syn --exclude cargo_metadata --exclude clap --exclude clap-cargo"
cargo update
cargo upgrade --incompatible $EXCLUSIONS
cargo generate-lockfile

# examples are their own independent crates, so we have to do them individually.
for folder in pgrx-examples/*; do
    if [ -d "$folder" ]; then
        cd $folder
        cargo update
        cargo upgrade --incompatible $EXCLUSIONS
        cargo generate-lockfile
        cargo check || exit $?
        cd -
    fi
done
