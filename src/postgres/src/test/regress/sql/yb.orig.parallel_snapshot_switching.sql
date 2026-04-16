--
-- Test snapshot switching in parallel workers (GH #30588).
--
-- In Read Committed isolation, calling a PL/pgSQL function that performs a
-- sub-query triggers a fresh transaction snapshot. When this happens inside
-- parallel workers, they independently create and switch to new snapshots.
--

SET max_parallel_workers_per_gather = 4;
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
SET yb_parallel_range_rows = 1;
SET yb_enable_cbo = on;

CREATE TABLE pss_test (k INT PRIMARY KEY, v TEXT) SPLIT INTO 5 TABLETS;
CREATE TABLE pss_dummy (k INT PRIMARY KEY, v INT);
INSERT INTO pss_test SELECT i, 'value_' || i FROM generate_series(1, 10000) i;
ALTER TABLE pss_test SET (parallel_workers = 4);
ANALYZE pss_test;

-- This function triggers a snapshot switch on each invocation in RC isolation,
-- because the SELECT inside the function acquires a new snapshot.
CREATE OR REPLACE FUNCTION pss_snapshot_fn(n INT) RETURNS INT AS $$
DECLARE
  v INT;
BEGIN
  SELECT pss_dummy.v INTO v FROM pss_dummy WHERE k = n;
  RETURN n;
END;
$$ LANGUAGE plpgsql;
ALTER FUNCTION pss_snapshot_fn(int) PARALLEL SAFE;

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parallel_snapshot_switching_query.sql'

SET DEFAULT_TRANSACTION_ISOLATION = 'READ COMMITTED';
\i :filename

SET DEFAULT_TRANSACTION_ISOLATION = 'REPEATABLE READ';
\i :filename

SET DEFAULT_TRANSACTION_ISOLATION = 'SERIALIZABLE';
\i :filename

-- Test with Gather Merge (sorted parallel output).
\set filename :abs_srcdir '/yb_commands/parallel_snapshot_switching_query_with_gather_merge.sql'
SET DEFAULT_TRANSACTION_ISOLATION = 'READ COMMITTED';
\i :filename

SET DEFAULT_TRANSACTION_ISOLATION = 'REPEATABLE READ';
\i :filename

SET DEFAULT_TRANSACTION_ISOLATION = 'SERIALIZABLE';
\i :filename

RESET max_parallel_workers_per_gather;
RESET parallel_setup_cost;
RESET parallel_tuple_cost;
RESET yb_parallel_range_rows;
RESET yb_enable_cbo;
