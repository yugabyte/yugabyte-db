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
# Simple linter for regress schedule.
set -u

. "${BASH_SOURCE%/*}/util.sh"

# Check schedule style: tests should match pattern "test: <test_name>".  That
# is, no inline comments and no parallel tests.
lines=$(diff \
          <(grep '^test: ' "$1") \
          <(grep -E '^test: [._[:alnum:]-]+$' "$1") \
          | grep '^<' \
          | sed 's/^< //')
if [ -n "$lines" ]; then
  while read -r line; do
    grep -nF "$line" "$1" \
      | sed 's/^/error:test_line_format:'\
'Should be formatted "^test<colon><space><test_name>$":/'
  done <<<"$lines"

  # These lines will doubly fail with the below ordering check, so exit early.
  exit
fi

# Check schedule test ordering:
# For ported tests (those beginning with "yb.port.") and original tests (those
# beginning without "yb."), they should be ordered the same way as in the
# upstream schedule.  For now, YB does not allow parallel tests, so when the
# upstream schedule has a parallel group, then YB schedules should flatten that
# to multiple lines.  For now, enforce ordering to be the same as the order the
# tests are listed left-to-right in the same parallel group, even if that is
# not strictly required.  Ignore some tests:
# - yb.port.numeric_big: this is in GNUmakefile instead of parallel_schedule
if [[ "$1" == src/postgres/src/test/regress/* ]]; then
  upstream_schedule="src/postgres/src/test/regress/parallel_schedule"
elif [[ "$1" == src/postgres/src/test/isolation/* ]]; then
  upstream_schedule="src/postgres/src/test/isolation/isolation_schedule"
else
  upstream_schedule=""
fi
if [ -n "$upstream_schedule" ]; then
  lines=$(diff \
            <(perl -ne 'print if '\
'/^test: (yb\.port\.(?!numeric_big$)|(?!yb\.))/' "$1" \
                | sed -e 's/test: yb\.port\.//' -e 's/test: //') \
            <(grep '^test: ' "$upstream_schedule" \
                | sed 's/test: //' | tr ' ' '\n') \
            | grep '^<' \
            | sed 's/< /test: \(yb\.port\.\)?/')
  if [ -n "$lines" ]; then
    set -e
    while read -r line; do
      grep -nE "$line" "$1" \
        | sed 's,^,error:test_ordering:'\
"Should follow $upstream_schedule test order:,"
    done <<<"$lines"
    set +e
  fi
fi
