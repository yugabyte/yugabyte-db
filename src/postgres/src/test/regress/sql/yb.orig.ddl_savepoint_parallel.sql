--
-- GH #30978 with DDL savepoints on: once a DDL has run in a transaction
-- block, no query in the block launches parallel workers, even after
-- ROLLBACK TO a savepoint that undoes the DDL: ROLLBACK TO does not clear the
-- block's DDL state. Companion of yb.orig.ddl_txn_parallel, which says why
-- the block launches no workers, and
-- whose schedule turns DDL savepoint support off (#33497).
--
SET yb_enable_cbo = on;
SET force_parallel_mode = on;
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
SET min_parallel_table_scan_size = 0;
SET yb_parallel_range_rows = 1;

CREATE TABLE ddl_sp_par (id INT, PRIMARY KEY (id ASC));
INSERT INTO ddl_sp_par SELECT i FROM generate_series(1, 1000) i;
ANALYZE ddl_sp_par;

-- ROLLBACK TO a savepoint taken before the DDL: the DDL is undone, but the
-- block still launches no workers.
BEGIN;
SAVEPOINT s1;
ALTER TABLE ddl_sp_par ADD COLUMN c TEXT DEFAULT 'g';
ROLLBACK TO SAVEPOINT s1;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_sp_par;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  SELECT COUNT(*) FROM ddl_sp_par;
SELECT COUNT(*) FROM ddl_sp_par;
COMMIT;

-- Once the block ends, the query gets a parallel plan again.
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_sp_par;

-- GH #30978 as filed: REPEATABLE READ, a DDL, a savepoint, more DDL after
-- the savepoint, then ROLLBACK TO it. The DDL before the savepoint stays, so
-- a worker would not see column c.
BEGIN ISOLATION LEVEL REPEATABLE READ;
ALTER TABLE ddl_sp_par ADD COLUMN c TEXT DEFAULT 'g';
SAVEPOINT s2;
CREATE INDEX ddl_sp_par_c ON ddl_sp_par (c);
UPDATE ddl_sp_par SET c = 'special' WHERE id <= 10;
ROLLBACK TO SAVEPOINT s2;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_sp_par WHERE c = 'g';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  SELECT COUNT(*) FROM ddl_sp_par WHERE c = 'g';
SELECT COUNT(*) FROM ddl_sp_par WHERE c = 'g';
ROLLBACK;

DROP TABLE ddl_sp_par;
RESET force_parallel_mode;
