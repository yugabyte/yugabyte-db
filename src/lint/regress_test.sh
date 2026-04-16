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

. "${BASH_SOURCE%/*}/common.sh"

# Ensure upstream tests match upstream.
if ! [[ "$1" =~ /yb[^/]+$ ]] && \
   [[ "$1" != src/postgres/yb-extensions/* ]]; then
  diff_result=$("${BASH_SOURCE%/*}"/diff_file_with_upstream.py "$1")
  exit_code=$?
  if [ $exit_code -ne 0 ]; then
    if [ $exit_code -ne 2 ]; then
      # The following messages are not emitted to stderr because those messages
      # may be buried under a large python stacktrace also emitted to stderr.
      if [ -z "$diff_result" ]; then
        echo "Unexpected failure, exit code $exit_code"
      else
        echo "$diff_result"
      fi
      exit 1
    fi

    echo 'error:bad_filename_for_yb_file:'\
'This is a YB-introduced file, but the linter does not recognize it as such.'\
' If it is YB-introduced, rename the file better (such as with "yb" prefix).'\
' If the file is from an upstream repository, update'\
' upstream_repositories.csv. The corresponding commit in'\
' upstream_repositories.csv should exist either locally in ~/code/<repo_name>'\
' or remotely in the corresponding remote repository (and you need internet'\
' access in that case).:1:'"$(head -1)"
  else
    grep -Eo '^[0-9]+' <<<"$diff_result" \
      | while read -r lineno; do
          echo 'error:upstream_regress_test_modified:'\
'Upstream-owned regress test should not be modified,'\
' or upstream_repositories.csv should be updated.:'\
"$lineno:$(sed -n "$lineno"p "$1")"
        done
  fi

  # Remaining rules do not apply to this file as they are for non-upstream
  # tests.
  exit
fi

# Trailing whitespace.  Avoid enforcing it for lines not owned by YB such as
# ported test lines without "yb" in them.
if [[ "$1" != *.out ]]; then
  sed_pattern='s/^/error:trailing_whitespace:Remove trailing whitespace:/'
  if [[ "$1" =~ /yb\.port\.[^/]+$ ]]; then
    grep -nE '(YB|Yb|yb).*\s+$' "$1" \
      | sed "$sed_pattern"
  else
    # Dependency tests may have lines ported from upstream PG, but it is rare
    # for such lines to have trailing whitespace.
    grep -nE '\s+$' "$1" \
      | sed "$sed_pattern"
  fi
fi

if ! [[ "$1" =~ (expected/[^/]+\.out|specs/[^/]+\.spec|sql/[^/]+\.sql|\
pg_partman/test/.*\.sql)$ ]]; then
  echo 'error:bad_regress_test_file_extension:'\
"${1##*/} has unexpected extension:1:$(head -1 "$1")"
fi

# Check that the test exists in a schedule.  pg_partman uses pg_prove instead
# of pg_regress schedules, so exempt it.
if [[ "$1" != src/postgres/third-party-extensions/pg_partman/* ]]; then
  schedules_dir=${1%/*}
  while [[ "$schedules_dir" == */* ]] &&
        [ -z "$(find "$schedules_dir" -name '*schedule')" ]; do
    schedules_dir=${schedules_dir%/*}
  done
  if [[ "$schedules_dir" != */* ]]; then
    echo "Failed to find schedule for $1" >&2
    exit 1
  fi
  found=false
  test_name=${1##*/}
  test_name=${test_name/_[0-9].out/.out}
  test_name=${test_name%.*}
  # TODO(jason): ysql_dump and backup_restore tests currently share the main
  # regress dir.  They should be placed in a separate dedicated directory
  # unaffiliated with the main regress dir.  For now, ignore these tests for
  # this lint rule.
  if [[ "$test_name" != yb.orig.ysql_dump* &&
        "$test_name" != yb.orig.backup_restore* ]]; then
    while read -r filepath; do
      if grep -qxF "test: $test_name" "$filepath"; then
        found=true
        break
      fi
    done < <(find "$schedules_dir" -name '*schedule')
    if ! "$found"; then
      echo 'error:dangling_regress_test:'\
    "$test_name not found in a schedule:1:$(head -1 "$1")"
    fi
  fi
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
        | grep -Eo '^[0-9]+' \
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
elif [[ "$1" =~ /yb[^/]+$ ]] && ! [[ "$1" =~ /yb.(depd|orig).[^/]+$ ]]; then
  echo 'error:bad_regress_test_file_prefix:'\
"${1##*/}"' has "yb" prefix but does not fit into any known category among '\
'"yb.depd.", "yb.orig.", or "yb.port.":1:'"$(head -1 "$1")"
fi
