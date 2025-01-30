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
# Simple linter for checking the filename of *.h files in src/yb/yql.
set -euo pipefail

. "${BASH_SOURCE%/*}/util.sh"

if grep -q '^extern "C" {$' "$1"; then
  if [[ "$1" != */ybc_*.h ]]; then
    echo 'error:missing_ybc_in_filename:'\
'This file should have "ybc_" prefix because of extern "C":'\
"1:$(head -1 "$1")"
  fi
else
  if [[ "$1" == */ybc_*.h ]]; then
    echo 'error:bad_ybc_in_filename:'\
'This file should not have "ybc_" prefix because of lack of extern "C":'\
"1:$(head -1 "$1")"
  fi
fi
