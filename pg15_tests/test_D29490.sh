#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressPgMisc
grep_in_java_test \
  "failed tests: [yb_pg_create_operator, yb_pg_create_table, yb_pg_create_type, yb_pg_with]" \
  TestPgRegressPgMisc
failing_java_test TestPgRegressTrigger
grep_in_java_test \
  "failed tests: [yb_pg_event_trigger, yb_pg_triggers, yb_triggers]" \
  TestPgRegressTrigger
# TODO(jason): below sometimes fails on pgstat Assert(!ps->dropped); crash.
#[ "$(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_pg_triggers.out | head -1)" = "447,448c447,448" ]
