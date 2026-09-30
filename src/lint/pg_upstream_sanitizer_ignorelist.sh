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
# Checks that code listed in build-support/pg_upstream_sanitizer_ignorelist.txt as identical to
# upstream has not changed, since a change must turn sanitizer checks back on for it.
set -euo pipefail

exec python3 "${BASH_SOURCE%/*}/../../python/yugabyte/pg_upstream_sanitizer_ignorelist.py" \
  check "$1"
