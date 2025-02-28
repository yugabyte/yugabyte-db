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
# Simple linter for regress test.
set -u

. "${BASH_SOURCE%/*}/util.sh"

if ! [[ "$1" =~ /yb[^/]+$ ]]; then
  echo "Unexpected file $1" >&2
  exit 1
fi

if ! [[ "$1" =~ (expected/[^/]+\.out|specs/[^/]+\.spec|sql/[^/]+\.sql|\
pg_partman/test/.*\.sql)$ ]]; then
  echo 'error:bad_regress_test_file_extension:'\
"${1##*/} has unexpected extension:1:$(head -1 "$1")"
fi

if [[ "$1" =~ /yb.port.[^/]+$ ]]; then
  # Remove "yb.port." prefix.
  pg_orig_test=${1/yb.port./}

  if ! [ -f "$pg_orig_test" ]; then
    # This ported test might have an expectfile suffix with no matching suffix
    # in the original tests.  In that case, consider the non-suffix'd test to
    # be the original test.

    # Remove alternative expectfile suffix.
    pg_orig_test=$(sed -E 's/_[0-9].out/.out/' <<<"$pg_orig_test")

    if ! [ -f "$pg_orig_test" ]; then
      echo 'error:original_regress_test_missing:'\
"Regress test $pg_orig_test does not exist:1:$(head -1 "$1")"
    fi
  fi

  case "$1" in
    *.spec|*.sql)
      # Check that all differing lines in the ported test have "yb" (case
      # insensitive) in them.  The commands are a bit complicated and can be
      # broken down as follows:
      # 1. Find hunks on the ported side that contain at least one line that
      #    doesn't contain "yb" (case insensitive).  This is represented by
      #    $line_ranges.
      # 1. For each hunk, find the lines that don't contain "yb" (case
      #    insensitive) and throw a lint message for them.
      diff "$1" "$pg_orig_test" \
        | perl -ne 'print if /^(< (?!.*(YB|Yb|yb))|\d)/' \
        | grep -B1 '^<' \
        | grep -Eo '^[0-9]+,[0-9]+' \
        | while read -r line_ranges; do
            grep -n '' "$1" \
              | sed -n "$line_ranges"p \
              | grep -vE 'YB|Yb|yb' \
              | while read -r line; do
                  if grep -E '^[0-9]+:--' <<<"$line"; then
                    suffix=\
' If your intention is to comment out a PG original test line, '\
'then use "/* YB..." and "*/ -- YB" lines before and after instead.'
                  else
                    suffix=
                  fi
                  echo 'warning:unexpected_line_in_ported_test:'\
'Regress test '"$pg_orig_test"' does not have this line, '\
'and this line does not have "yb" in it.'"$suffix:$line"
                done
          done
      ;;
    *.out)
      # For output files, it is hard to add "yb" to output lines, so use a
      # simpler heuristic of checking for 50% similarity with the ported test.
      # Note that this check does not ignore "yb" lines, so having too many
      # "yb" lines may trigger this lint message.  That is a desirable
      # side-effect.
      differing_line_count=$(diff -d "$1" "$pg_orig_test" | grep -c '^<')
      total_line_count=$(wc -l "$1" | awk '{print$1}')
      if [ "$differing_line_count" -gt $((total_line_count / 2)) ]; then
        echo 'error:too_many_differences_in_ported_test:'\
'More than 50% of the lines in this ported test '\
'are different from the original test '"$pg_orig_test:1:$(head -1 "$1")"
      fi
      ;;
    *)
      echo "Unexpected case for $1" >&2
      exit 1
  esac
elif ! [[ "$1" =~ /yb.(depd|orig).[^/]+$ ]]; then
  echo 'error:bad_regress_test_file_prefix:'\
"${1##*/}"' has "yb" prefix but does not fit into any known category among '\
'"yb.depd.", "yb.orig.", or "yb.port.":1:'"$(head -1 "$1")"
fi
