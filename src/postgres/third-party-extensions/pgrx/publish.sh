#! /bin/sh
#LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
#LICENSE
#LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
#LICENSE
#LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
#LICENSE
#LICENSE All rights reserved.
#LICENSE
#LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
 

DIR=`pwd`
set -x

# dependency graph, from roots to facades
#
# pgrx-pg-config
# ├── cargo-pgrx
# ├── pgrx-bindgen
# │   [build-dependencies]
# │   └── pgrx-pg-sys
# │       └── pgrx
# │           └── pgrx-tests
# └── pgrx-tests
#
# pgrx-sql-entity-graph
# ├── cargo-pgrx
# ├── pgrx
# │   └── pgrx-tests
# ├── pgrx-macros
# │   ├── pgrx
# │   ├── pgrx-pg-sys
# │   │   └── pgrx
# │   └── pgrx-tests
# └── pgrx-pg-sys


cd $DIR/pgrx-pg-config && cargo publish
cd $DIR/pgrx-bindgen && cargo publish
cd $DIR/pgrx-sql-entity-graph && cargo publish
cd $DIR/pgrx-macros && cargo publish
cd $DIR/pgrx-pg-sys && cargo publish --no-verify
cd $DIR/pgrx && cargo publish --no-verify
cd $DIR/pgrx-tests && cargo publish --no-verify
cd $DIR/cargo-pgrx && cargo publish # cargo-pgrx last so the templates are correct
