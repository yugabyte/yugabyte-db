#!/usr/bin/env bash
# Lint a regress test schedule.
#
# Exit code 1: For argument or filesystem issues, return exit code 1.
#
# Exit code 2: If a schedule has test lines that don't match pattern "test:
# <test_name>", output those lines and return exit code 2.
#
# Exit code 3: If ported tests of the schedule don't match the ordering of
# parallel_schedule, output those lines and return exit code 3.

# Switch to script dir.
cd "${BASH_SOURCE%/*}" || exit 1

# Check args.
if [ $# -ne 1 ]; then
  echo "incorrect number of arguments: $#" >&2
  exit 1
fi
schedule=$1
if [ ! -f "$schedule" ]; then
  echo "schedule does not exist: $schedule" >&2
  exit 1
fi

# Check schedule style: tests should match pattern "test: <test_name>".
TESTS=$(diff \
          <(grep '^test: ' "$schedule") \
          <(grep -E '^test: [._[:alnum:]]+$' "$schedule") \
        | grep '^<' \
        | sed 's/^< //')
if [ -n "$TESTS" ]; then
  echo "$TESTS"
  exit 2
fi

# Check schedule test ordering:
# For ported tests (those beginning with "yb_pg_"), they should be ordered the
# same way as in parallel_schedule.  For now, also enforce ordering to be the
# same as tests in the same group, even if it is not strictly required.  Ignore
# some tests:
# - yb_pg_numeric_big: this is in GNUmakefile instead of parallel_schedule
# - yb_pg_stat: this is a YB test, not ported: prefix "yb_" + name "pg_stat"
# - yb_pg_stat_backend: this is a YB test, not ported
TESTS=$(diff \
          <(grep '^test: yb_pg_' "$schedule" | sed 's/test: yb_pg_//') \
          <(grep '^test: ' parallel_schedule | sed 's/test: //' | tr ' ' '\n') \
        | grep '^<' \
        | sed 's/< /test: yb_pg_/' \
        | grep -Ev '^test: yb_pg_(numeric_big$|stat$|stat_backend$)')
if [ -n "$TESTS" ]; then
  echo "$TESTS"
  exit 3
fi
