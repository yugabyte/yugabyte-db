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
# Simple linter to make sure a file is uniquely sorted.
set -u

. "${BASH_SOURCE%/*}/util.sh"

pattern='YB|Yb|yb'

if [[ "$1" == */yb_typedefs.list ]]; then
  grep -Env "$pattern" "$1" \
    | sed 's/^/error:missing_yb_in_type_name:'\
'Types in yb_typedefs.list should have "yb":/'
else
  grep -En "$pattern" "$1" \
    | sed 's/^/error:bad_yb_in_type_name:'\
'Types in non-yb_typedefs.list should not have "yb":/'
fi
