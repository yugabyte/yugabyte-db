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
# Simple linter for src/yb/yql/***/ybc_*.h files.
set -euo pipefail

. "${BASH_SOURCE%/*}/common.sh"

check_ctags
echo "$1" \
  | ctags -n -L - --languages=c,c++ --c-kinds=t --c++-kinds=t -f /dev/stdout \
  | while read -r line; do
      symbol=$(echo "$line" | cut -f1)
      lineno=$(echo "$line" | cut -f3 | grep -Eo '^[0-9]+')

      if [[ "$symbol" != Ybc* ]]; then
        echo 'error:missing_ybc_prefix:This type should have "Ybc" prefix:'\
"$lineno:$(sed -n "$lineno"p "$1")"
      fi

      if grep -q "^} $symbol;" "$1"; then
        # This "%" command on "}" is not language aware, so it may not
        # correctly find the corresponding opening "{" if comments have
        # non-matching braces.  Perhaps make this language aware in the future,
        # or adjust such comments.
        first_line=$(vi -ens +'0' +"/^} $symbol;" +'normal 0%' +'0,.-1d' \
                       +'.1,$d' +'w! /dev/stdout' +'q' "$1")
        # In the common case, there is no name following enum/struct/union, so
        # there is nothing to check.
        if grep -Eq "^typedef (enum|struct|union) {" <<<"$first_line"; then
          continue
        fi
        # In case of #ifdef __cplusplus, a name is required after
        # enum/struct/union, and it should match the typedef name.
        other_symbol=$(sed -E 's/^typedef (enum|struct|union) ([_[:alnum:]]+) \{$/\2/' \
                         <<<"$first_line")
      elif grep -Eq "^typedef (enum|struct|union) \\w+ $symbol;" "$1"; then
        other_symbol=$(sed -n "$lineno"p "$1" \
                         | sed -E \
                             's/^typedef (enum|struct|union) ([_[:alnum:]]+) [_[:alnum:]]+;/\2/')
      else
        # There is no other symbol involved.  Set this just to pass the below
        # check.
        other_symbol=$symbol
      fi
      if [[ "$other_symbol" != "$symbol" ]]; then
        echo 'error:mismatching_typename:'\
'The typedef name should match the enum/struct/union name:'\
"$lineno:$(sed -n "$lineno"p "$1")"
      fi
    done
