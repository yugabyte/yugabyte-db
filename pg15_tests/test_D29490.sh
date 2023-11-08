#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

java_test TestPgRegressTabletSplit
java_test TestPgRegressPgMisc false
grep_in_java_test \
  "failed tests: [yb_pg_create_function_2, yb_pg_create_operator, yb_pg_create_table, yb_pg_create_type, yb_pg_int4, yb_pg_int8, yb_pg_with]" \
  TestPgRegressPgMisc
java_test TestPgRegressTrigger false
grep_in_java_test \
  "failed tests: [yb_pg_event_trigger, yb_pg_triggers, yb_triggers]" \
  TestPgRegressTrigger
# TODO(jason): below sometimes fails on pgstat Assert(!ps->dropped); crash.
#[ "$(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_pg_triggers.out | head -1)" = "447,448c447,448" ]
