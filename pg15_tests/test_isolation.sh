#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgIsolationRegress
grep_in_java_test \
  "failed tests: [ensure-lock-only-conflict-always-ignored, ignore-intent-of-rolled-back-savepoint-cross-txn, yb-modification-followed-by-lock, yb-skip-locked-after-update, yb-skip-locked-single-shard-transaction, yb_conflict_with_index_only_scan_in_serializable, yb_pg_eval-plan-qual, yb_read_committed_insert, yb_read_committed_insert_duplicate_key_error, yb_read_committed_test_do_call, yb_read_committed_test_internal_savepoint, yb_read_committed_update_and_explicit_locking]" \
  TestPgIsolationRegress
