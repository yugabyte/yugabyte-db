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
# Linter for YSQL migration SQL files under src/yb/yql/pgwrapper/ysql_migrations/.
# Warns when a new migration SQL file is added but pg_yb_migration.dat was not
# updated, which may cause brownfield upgrade failures.
set -u
exec 2>/dev/null

. "${BASH_SOURCE%/*}/common.sh"

MIGRATION_DAT="src/postgres/src/include/catalog/pg_yb_migration.dat"

set_merge_base || exit 0

# Returns 0 if $1 is a newly added file on this branch (committed or staged),
# 1 otherwise. Modifications to existing migration files do not change the
# migration version and therefore do not require a pg_yb_migration.dat update.
is_newly_added() {
  lint_git_diff "$merge_base" --diff-filter=A --name-only -- "$1" | grep -q .
}

# Returns 0 if pg_yb_migration.dat was modified anywhere on this branch
# (committed or staged), 1 otherwise.
migration_dat_updated() {
  lint_git_diff "$merge_base" -- "$MIGRATION_DAT" | grep -q '^+'
}

if ! is_newly_added "$1"; then
  exit 0
fi

if migration_dat_updated; then
  exit 0
fi

# Emit a file-level warning (no line number).
echo "warning:ysql_migration:\
New YSQL migration SQL file added but $MIGRATION_DAT was not updated. \
Please bump up the version numbers in $MIGRATION_DAT to match the new migration::"
