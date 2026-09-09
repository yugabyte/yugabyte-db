--
-- Test cumulative stats system
--
-- Must be run after tenk2 has been created (by create_table),
-- populated (by create_misc) and indexed (by create_index).
-- YB: skip tenk2-dependent seq/idx scan and heap-block counters; YB does
-- not maintain them like PostgreSQL.
--

-- conditio sine qua non
SHOW track_counts;  -- must be on

-- ensure that both seqscan and indexscan plans are allowed
SET enable_seqscan TO on;
SET enable_indexscan TO on;
-- for the moment, we don't want index-only scans here
SET enable_indexonlyscan TO off;
-- not enabled by default, but we want to test it...
SET track_functions TO 'all';

-- record dboid for later use
SELECT oid AS dboid from pg_database where datname = current_database() \gset

-- YB: skip prevstats / TRUNCATE n_tup_* / seqscan / idxscan / heap_blks.
-- Those counters stay 0 or disagree with PG. Port remaining queries when
-- they are accurate.

----
-- Basic tests for track_functions
---
CREATE FUNCTION stats_test_func1() RETURNS VOID LANGUAGE plpgsql AS $$BEGIN END;$$;
SELECT 'stats_test_func1()'::regprocedure::oid AS stats_test_func1_oid \gset
CREATE FUNCTION stats_test_func2() RETURNS VOID LANGUAGE plpgsql AS $$BEGIN END;$$;
SELECT 'stats_test_func2()'::regprocedure::oid AS stats_test_func2_oid \gset

-- test that stats are accumulated
BEGIN;
SET LOCAL stats_fetch_consistency = none;
SELECT pg_stat_get_function_calls(:stats_test_func1_oid);
SELECT pg_stat_get_xact_function_calls(:stats_test_func1_oid);
SELECT stats_test_func1();
SELECT pg_stat_get_xact_function_calls(:stats_test_func1_oid);
SELECT stats_test_func1();
SELECT pg_stat_get_xact_function_calls(:stats_test_func1_oid);
SELECT pg_stat_get_function_calls(:stats_test_func1_oid);
COMMIT;

-- Verify that function stats are not transactional

-- rolled back savepoint in committing transaction
BEGIN;
SELECT stats_test_func2();
SAVEPOINT foo;
SELECT stats_test_func2();
ROLLBACK TO SAVEPOINT foo;
SELECT pg_stat_get_xact_function_calls(:stats_test_func2_oid);
SELECT stats_test_func2();
COMMIT;

-- rolled back transaction
BEGIN;
SELECT stats_test_func2();
ROLLBACK;

SELECT pg_stat_force_next_flush();

-- check collected stats
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func1_oid;
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func2_oid;


-- check that a rolled back drop function stats leaves stats alive
BEGIN;
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func1_oid;
DROP FUNCTION stats_test_func1();
-- shouldn't be visible via view
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func1_oid;
-- but still via oid access
SELECT pg_stat_get_function_calls(:stats_test_func1_oid);
ROLLBACK;
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func1_oid;
SELECT pg_stat_get_function_calls(:stats_test_func1_oid);


-- check that function dropped in main transaction leaves no stats behind
BEGIN;
DROP FUNCTION stats_test_func1();
COMMIT;
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func1_oid;
SELECT pg_stat_get_function_calls(:stats_test_func1_oid);

-- check that function dropped in a subtransaction leaves no stats behind
BEGIN;
SELECT stats_test_func2();
SAVEPOINT a;
SELECT stats_test_func2();
SAVEPOINT b;
DROP FUNCTION stats_test_func2();
COMMIT;
SELECT funcname, calls FROM pg_stat_user_functions WHERE funcid = :stats_test_func2_oid;
SELECT pg_stat_get_function_calls(:stats_test_func2_oid);


-- YB: skip DROP TABLE live-tuple / tuples-inserted counters. YB does not
-- maintain n_live_tup / n_tup_ins on heap stats like PostgreSQL.

-----
-- Test reset of some stats for shared table
-----

-- YB: skip pg_shdescription n_tup_ins/n_tup_upd. Catalog tuple counters
-- stay 0 on YB.

-----
-- Test that various stats views are being properly populated
-----

-- Test that sessions is incremented when a new session is started in pg_stat_database
SELECT sessions AS db_stat_sessions FROM pg_stat_database WHERE datname = (SELECT current_database()) \gset
\c
SELECT pg_stat_force_next_flush();
SELECT sessions > :db_stat_sessions FROM pg_stat_database WHERE datname = (SELECT current_database());

-- YB: skip pg_stat_bgwriter checkpoints_req and pg_stat_wal wal_bytes.
-- CHECKPOINT is ignored on YB; there is no PostgreSQL WAL.

-----
-- Test that resetting stats works for reset timestamp
-----

-- Test that reset_slru with a specified SLRU works.
SELECT stats_reset AS slru_commit_ts_reset_ts FROM pg_stat_slru WHERE name = 'CommitTs' \gset
SELECT stats_reset AS slru_notify_reset_ts FROM pg_stat_slru WHERE name = 'Notify' \gset
SELECT pg_stat_reset_slru('CommitTs');
SELECT stats_reset > :'slru_commit_ts_reset_ts'::timestamptz FROM pg_stat_slru WHERE name = 'CommitTs';
SELECT stats_reset AS slru_commit_ts_reset_ts FROM pg_stat_slru WHERE name = 'CommitTs' \gset

-- Test that multiple SLRUs are reset when no specific SLRU provided to reset function
SELECT pg_stat_reset_slru(NULL);
SELECT stats_reset > :'slru_commit_ts_reset_ts'::timestamptz FROM pg_stat_slru WHERE name = 'CommitTs';
SELECT stats_reset > :'slru_notify_reset_ts'::timestamptz FROM pg_stat_slru WHERE name = 'Notify';

-- Test that reset_shared with archiver specified as the stats type works
SELECT stats_reset AS archiver_reset_ts FROM pg_stat_archiver \gset
SELECT pg_stat_reset_shared('archiver');
SELECT stats_reset > :'archiver_reset_ts'::timestamptz FROM pg_stat_archiver;
SELECT stats_reset AS archiver_reset_ts FROM pg_stat_archiver \gset

-- Test that reset_shared with bgwriter specified as the stats type works
SELECT stats_reset AS bgwriter_reset_ts FROM pg_stat_bgwriter \gset
SELECT pg_stat_reset_shared('bgwriter');
SELECT stats_reset > :'bgwriter_reset_ts'::timestamptz FROM pg_stat_bgwriter;
SELECT stats_reset AS bgwriter_reset_ts FROM pg_stat_bgwriter \gset

-- Test that reset_shared with wal specified as the stats type works
SELECT stats_reset AS wal_reset_ts FROM pg_stat_wal \gset
SELECT pg_stat_reset_shared('wal');
SELECT stats_reset > :'wal_reset_ts'::timestamptz FROM pg_stat_wal;
SELECT stats_reset AS wal_reset_ts FROM pg_stat_wal \gset

-- Test that reset_shared with no specified stats type doesn't reset anything
SELECT pg_stat_reset_shared(NULL);
SELECT stats_reset = :'archiver_reset_ts'::timestamptz FROM pg_stat_archiver;
SELECT stats_reset = :'bgwriter_reset_ts'::timestamptz FROM pg_stat_bgwriter;
SELECT stats_reset = :'wal_reset_ts'::timestamptz FROM pg_stat_wal;

-- Test that reset works for pg_stat_database

-- Since pg_stat_database stats_reset starts out as NULL, reset it once first so we have something to compare it to
SELECT pg_stat_reset();
SELECT stats_reset AS db_reset_ts FROM pg_stat_database WHERE datname = (SELECT current_database()) \gset
SELECT pg_stat_reset();
SELECT stats_reset > :'db_reset_ts'::timestamptz FROM pg_stat_database WHERE datname = (SELECT current_database());


----
-- pg_stat_get_snapshot_timestamp behavior
----
BEGIN;
SET LOCAL stats_fetch_consistency = snapshot;
-- no snapshot yet, return NULL
SELECT pg_stat_get_snapshot_timestamp();
-- any attempt at accessing stats will build snapshot
SELECT pg_stat_get_function_calls(0);
SELECT pg_stat_get_snapshot_timestamp() >= NOW();
-- shows NULL again after clearing
SELECT pg_stat_clear_snapshot();
SELECT pg_stat_get_snapshot_timestamp();
COMMIT;

----
-- Changing stats_fetch_consistency in a transaction.
----
BEGIN;
-- Stats filled under the cache mode
SET LOCAL stats_fetch_consistency = cache;
SELECT pg_stat_get_function_calls(0);
SELECT pg_stat_get_snapshot_timestamp() IS NOT NULL AS snapshot_ok;
-- Success in accessing pre-existing snapshot data.
SET LOCAL stats_fetch_consistency = snapshot;
SELECT pg_stat_get_snapshot_timestamp() IS NOT NULL AS snapshot_ok;
SELECT pg_stat_get_function_calls(0);
SELECT pg_stat_get_snapshot_timestamp() IS NOT NULL AS snapshot_ok;
-- Snapshot cleared.
SET LOCAL stats_fetch_consistency = none;
SELECT pg_stat_get_snapshot_timestamp() IS NOT NULL AS snapshot_ok;
SELECT pg_stat_get_function_calls(0);
SELECT pg_stat_get_snapshot_timestamp() IS NOT NULL AS snapshot_ok;
ROLLBACK;

-- YB: skip pg_stat_have_stats. pg_stat_have_stats('bgwriter', 0, 0)
-- terminates the backend.

-- ensure that stats accessors handle NULL input correctly
SELECT pg_stat_get_replication_slot(NULL);
SELECT pg_stat_get_subscription_stats(NULL);

-- YB: last_analyze vs last_autoanalyze. PG records auto-analyze only from
-- autovacuum workers; YB auto-analyze uses
-- yb_use_internal_auto_analyze_service_conn (or YB_AUTO_ANALYZE_BACKEND).
CREATE TABLE pgstat_analyze_stats (k int PRIMARY KEY);
SELECT last_analyze IS NOT NULL AS has_last_analyze,
       last_autoanalyze IS NOT NULL AS has_last_autoanalyze,
       analyze_count, autoanalyze_count
  FROM pg_stat_user_tables WHERE relname = 'pgstat_analyze_stats';
ANALYZE pgstat_analyze_stats;
SELECT last_analyze IS NOT NULL AS has_last_analyze,
       last_autoanalyze IS NOT NULL AS has_last_autoanalyze,
       analyze_count, autoanalyze_count
  FROM pg_stat_user_tables WHERE relname = 'pgstat_analyze_stats';
SET yb_use_internal_auto_analyze_service_conn = true;
ANALYZE pgstat_analyze_stats;
RESET yb_use_internal_auto_analyze_service_conn;
SELECT last_analyze IS NOT NULL AS has_last_analyze,
       last_autoanalyze IS NOT NULL AS has_last_autoanalyze,
       analyze_count, autoanalyze_count
  FROM pg_stat_user_tables WHERE relname = 'pgstat_analyze_stats';
DROP TABLE pgstat_analyze_stats;

-- End of Stats Test
