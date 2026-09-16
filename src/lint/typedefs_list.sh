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

. "${BASH_SOURCE%/*}/common.sh"

pattern='YB|Yb|yb'

if [[ "$1" == */yb_typedefs.list ]]; then
  grep -Env "$pattern" "$1" \
    | sed 's/^/error:missing_yb_in_type_name:'\
'Types in yb_typedefs.list should have "yb":/'

  grep -no '^Form_[a-zA-Z0-9_]*' "$1" \
    | while IFS=: read -r lineno form; do
    formdata="FormData_${form#Form_}"
    if ! grep -q "^$formdata$" "$1"; then
      echo "error:missing_formdata:$formdata is missing for $form:$lineno:"
    fi
  done

  grep -no '^FormData_[a-zA-Z0-9_]*' "$1" \
    | while IFS=: read -r lineno formdata; do
    form="Form_${formdata#FormData_}"
    if ! grep -q "^$form$" "$1"; then
      echo "error:missing_form:$form is missing for $formdata:$lineno:"
    fi
  done

  # Missing handle types: see the corresponding rule in ybc_pg.sh.
  macros=$(handle_type_macros) || exit 1
  handle_types=$(lint_git_grep -lE "$macros" src/yb/yql/pggate \
                   | xargs grep -hoE "($macros)\([A-Z][a-zA-Z0-9_]*\)" \
                   | sed 's/.*(//' \
                   | sed 's/)//' \
                   | sort -u)
  while read -r handle_type; do
    transformed_type="Ybc${handle_type}"
    if ! grep -q "^$transformed_type$" "$1"; then
      echo "error:missing_handle_type:yb_typedefs.list is missing \
$transformed_type for handle type $handle_type:1:"
    fi
  done <<<"$handle_types"

  # Every entry should name a type that exists.  Take the entries ctags cannot
  # see, then accept the ones a macro produces:
  # - a FormData_foo whose Form_foo ctags does find (from a CATALOG macro)
  # - a Ybcfoo from a handle type macro
  # Note: the sort is for comm, which needs both inputs ordered.  The
  # unique_sort lint rule reports a misordered list separately, but the two
  # rules run independently, so this cannot assume that one ran first.
  check_ctags || exit 1
  ctags_found=$(all_ctags_types)
  comm -13 <(printf '%s\n' "$ctags_found") <(sort -u "$1") \
    | while read -r entry; do
        case "$entry" in
          FormData_*)
            if [[ $'\n'"$ctags_found"$'\n' \
                  == *$'\n'"Form_${entry#FormData_}"$'\n'* ]]; then
              continue
            fi
            ;;
          Ybc*)
            if [[ $'\n'"$handle_types"$'\n' == *$'\n'"${entry#Ybc}"$'\n'* ]]; then
              continue
            fi
            ;;
        esac
        echo 'error:dangling_yb_typedef:'\
'No definition found for this type, so it should be removed from'\
' yb_typedefs.list:'"$(grep -nxF "$entry" "$1" | cut -d: -f1):$entry"
      done
else
  # typedefs.list is owned by upstream.  A YB-added type whose name has no "yb"
  # is only visible as a difference from upstream, not to the
  # bad_yb_in_type_name lint rule.
  if [[ "$1" == */typedefs.list ]]; then
    diff_result=$("${BASH_SOURCE%/*}"/diff_file_with_upstream.py "$1")
    exit_code=$?
    if [ $exit_code -ne 0 ]; then
      if [ $exit_code -eq 2 ]; then
        echo "Unexpected exit code 2"
      fi
      # The following messages are not emitted to stderr because those messages
      # may be buried under a large python stacktrace also emitted to stderr.
      if [ -z "$diff_result" ]; then
        echo "Unexpected failure, exit code $exit_code"
      else
        echo "$diff_result"
      fi
      exit 1
    fi

    grep -Eo '^[0-9]+' <<<"$diff_result" \
      | while read -r lineno; do
          echo 'error:upstream_typedefs_list_modified:'\
'Upstream-owned typedefs.list should not be modified. YB types belong in'\
' yb_typedefs.list and should have "yb" in the name, or if importing an'\
' upstream commit, upstream_repositories.csv should be updated.:'\
"$lineno:$(sed -n "$lineno"p "$1")"
        done
  fi

  grep -En "$pattern" "$1" \
    | sed 's/^/error:bad_yb_in_type_name:'\
'Types in non-yb_typedefs.list should not have "yb":/'
fi
