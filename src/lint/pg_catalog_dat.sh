#!/usr/bin/env bash
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# Linter for pg catalog .dat files under src/postgres/src/include/catalog/.
# Warns when:
#   1. A .dat file is modified but YB_LAST_USED_OID in catalog.h was not updated.
#   2. A .dat file is modified but no new SQL file was added to the YSQL migrations directory.
set -u
exec 2>/dev/null

. "${BASH_SOURCE%/*}/common.sh"

CATALOG_H="src/postgres/src/include/catalog/catalog.h"
MIGRATIONS_DIR="src/yb/yql/pgwrapper/ysql_migrations"

set_merge_base || exit 0

# Returns 0 if YB_LAST_USED_OID was bumped anywhere on this branch
# (committed or staged), 1 otherwise.
catalog_h_bumped() {
  lint_git_diff "$merge_base" -- "$CATALOG_H" | grep -q '^+.*YB_LAST_USED_OID'
}

# Returns 0 if at least one new file was added to the migrations directory
# anywhere on this branch (committed or staged), 1 otherwise.
migration_added() {
  lint_git_diff "$merge_base" --name-only --diff-filter=A -- "$MIGRATIONS_DIR" | grep -q .
}

# Each check emits its own file-level warning (no line number).
if ! catalog_h_bumped; then
  echo "warning:pg_catalog_dat:\
PG catalog .dat file modified but YB_LAST_USED_OID in catalog.h was not updated. \
If you assigned a new static OID please make sure to increment YB_LAST_USED_OID in $CATALOG_H::"
fi

if ! migration_added; then
  echo "warning:pg_catalog_dat:\
PG catalog .dat file modified but no new YSQL migration was added. \
Consider adding a migration file to $MIGRATIONS_DIR for upgrade safety::"
fi
