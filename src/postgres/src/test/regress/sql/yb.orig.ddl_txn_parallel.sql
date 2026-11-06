--
-- GH #30978: once a DDL has run in a transaction block, parallel plans in the
-- block launch no workers, and the leader runs the whole plan. This is needed
-- until parallel workers stop reading the catalog in legacy mode (see
-- YBCIsLegacyModeForCatalogOps). In legacy mode, workers do not see the
-- uncommitted catalog writes of earlier DDLs in the block. Plans stay
-- parallel: EXPLAIN shows the Gather, and EXPLAIN ANALYZE shows
-- Workers Launched: 0.
-- Scenario: BEGIN, ALTER TABLE ADD COLUMN c, then a scan that filters on c.
-- A worker launched there fails with "invalid attnum 3 for relation".
--
SET yb_enable_cbo = on;
SET force_parallel_mode = on;
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
SET min_parallel_table_scan_size = 0;
SET yb_parallel_range_rows = 1;

CREATE TABLE ddl_par (id INT, name TEXT, PRIMARY KEY (id ASC));
INSERT INTO ddl_par SELECT i, 'n' || i FROM generate_series(1, 1000) i;
ANALYZE ddl_par;

-- A generic plan cached before the block is parallel. The ALTER TABLE
-- invalidates it; the replanned plan is parallel too, and in the block it
-- launches no workers.
PREPARE ddl_par_count AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_count;

BEGIN;
ALTER TABLE ddl_par ADD COLUMN c TEXT DEFAULT 'g';
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par WHERE c = 'g';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  SELECT COUNT(*) FROM ddl_par WHERE c = 'g';
SELECT COUNT(*) FROM ddl_par WHERE c = 'g';
EXPLAIN (COSTS OFF) EXECUTE ddl_par_count;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_count;
EXECUTE ddl_par_count;
ROLLBACK;

DEALLOCATE ddl_par_count;

-- Once the block ends, queries launch workers again, whether the block
-- rolled back (above) or committed.
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par;
BEGIN;
CREATE TABLE ddl_par_commit (k INT);
COMMIT;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par;
DROP TABLE ddl_par_commit;

-- A generic plan first built after the DDL is parallel too, and launches
-- workers again once the block ends.
BEGIN;
CREATE TABLE ddl_par_after_t (k INT);
PREPARE ddl_par_after AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_after;
COMMIT;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_after;
DEALLOCATE ddl_par_after;
DROP TABLE ddl_par_after_t;

-- The same with an ALTER TABLE on a table the plan does not use.
CREATE TABLE ddl_par_other (k INT);
BEGIN;
ALTER TABLE ddl_par_other ADD COLUMN x INT;
PREPARE ddl_par_alter AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_alter;
COMMIT;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_alter;
DEALLOCATE ddl_par_alter;

-- The same after ROLLBACK.
BEGIN;
ALTER TABLE ddl_par_other ADD COLUMN y INT;
PREPARE ddl_par_rb AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_rb;
ROLLBACK;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_rb;
DEALLOCATE ddl_par_rb;

-- The same when the block's only DDL replaces a function the plan does not
-- call, after COMMIT and after ROLLBACK.
CREATE FUNCTION ddl_par_noop() RETURNS INT LANGUAGE sql AS 'SELECT 1';
BEGIN;
CREATE OR REPLACE FUNCTION ddl_par_noop() RETURNS INT LANGUAGE sql
  AS 'SELECT 2';
PREPARE ddl_par_fn_c AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_fn_c;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_fn_c;
COMMIT;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_fn_c;
DEALLOCATE ddl_par_fn_c;
BEGIN;
CREATE OR REPLACE FUNCTION ddl_par_noop() RETURNS INT LANGUAGE sql
  AS 'SELECT 3';
PREPARE ddl_par_fn_r AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_fn_r;
ROLLBACK;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_fn_r;
DEALLOCATE ddl_par_fn_r;
DROP FUNCTION ddl_par_noop();

-- Generic plans cached before the block keep their Gather and, after the
-- block's DDL, run in the leader with no workers. Without workers no shared scan state is set up, so each
-- parallel scan must read the whole table alone: both counts are 1000.
CREATE FUNCTION ddl_par_unused() RETURNS INT LANGUAGE sql AS 'SELECT 1';
PREPARE ddl_par_seq AS SELECT COUNT(*) FROM ddl_par WHERE name LIKE 'n%';
PREPARE ddl_par_idx AS SELECT COUNT(name) FROM ddl_par WHERE id > 0;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_seq;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_idx;
BEGIN;
CREATE OR REPLACE FUNCTION ddl_par_unused() RETURNS INT LANGUAGE sql
  AS 'SELECT 2';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_seq;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_idx;
EXECUTE ddl_par_seq;
EXECUTE ddl_par_idx;
ROLLBACK;
DEALLOCATE ddl_par_seq;
DEALLOCATE ddl_par_idx;

-- The same for a Gather Merge: no workers, and the leader still returns the
-- rows in order.
PREPARE ddl_par_gm AS SELECT name FROM ddl_par ORDER BY name LIMIT 3;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_gm;
BEGIN;
CREATE OR REPLACE FUNCTION ddl_par_unused() RETURNS INT LANGUAGE sql
  AS 'SELECT 3';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_gm;
EXECUTE ddl_par_gm;
ROLLBACK;
DEALLOCATE ddl_par_gm;
DROP FUNCTION ddl_par_unused();

-- CREATE INDEX in a block builds the index nonconcurrently and marks the
-- block like any other DDL: no workers after it.
BEGIN;
CREATE INDEX ddl_par_other_k ON ddl_par_other (k);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par;
ROLLBACK;
DROP TABLE ddl_par_other;

-- A DDL outside a block ends with its own implicit commit, so the next query
-- launches workers again.
CREATE TABLE ddl_par_auto (k INT);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par;
DROP TABLE ddl_par_auto;

-- GH #30978 as filed ran in REPEATABLE READ and rolled back to a savepoint
-- taken after the DDL. The DDL before the savepoint stays, so the block
-- still has uncommitted catalog rows and the query launches no workers.
-- This schedule turns DDL savepoint support off (#33497), so no DDL runs
-- after the savepoint here.
CREATE TABLE ddl_par_sp (id INT, PRIMARY KEY (id ASC));
INSERT INTO ddl_par_sp SELECT i FROM generate_series(1, 1000) i;
ANALYZE ddl_par_sp;
BEGIN ISOLATION LEVEL REPEATABLE READ;
ALTER TABLE ddl_par_sp ADD COLUMN c TEXT DEFAULT 'g';
SAVEPOINT s1;
UPDATE ddl_par_sp SET c = 'special' WHERE id <= 10;
ROLLBACK TO SAVEPOINT s1;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par_sp WHERE c = 'g';
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  SELECT COUNT(*) FROM ddl_par_sp WHERE c = 'g';
SELECT COUNT(*) FROM ddl_par_sp WHERE c = 'g';
ROLLBACK;
DROP TABLE ddl_par_sp;

-- A temp-table DDL also turns off parallel workers for the rest of the block,
-- even for queries on permanent tables. Workers never scan temp tables, but
-- they can still need the block's temp catalog rows, for example a pg_temp
-- function the query calls, so this is not only a cost. A generic plan made in
-- the block launches workers again after COMMIT.
BEGIN;
CREATE TEMP TABLE ddl_par_tmp (k INT);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par;
PREPARE ddl_par_tmp_q AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_tmp_q;
COMMIT;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_tmp_q;
DEALLOCATE ddl_par_tmp_q;
DROP TABLE ddl_par_tmp;

-- CREATE TABLE AS is a DDL, so it sets the flag before it runs its query.
-- With transactional DDL on, yb_disable_parallel_query_in_ddl = off then
-- gives it a parallel plan that launches no workers.
SET yb_disable_parallel_query_in_ddl = off;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  CREATE TABLE ddl_par_ctas AS SELECT COUNT(*) FROM ddl_par;
RESET yb_disable_parallel_query_in_ddl;
DROP TABLE ddl_par_ctas;

DROP TABLE ddl_par;

-- GH #34740: a parallel scan of a table created earlier in the same
-- transaction block must not fail with "relation with OID ... does not
-- exist".
BEGIN;
CREATE TABLE ddl_par_new (k INT) SPLIT INTO 1 TABLETS;
INSERT INTO ddl_par_new VALUES (1), (2);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par_new;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT COUNT(*) FROM ddl_par_new;
SELECT COUNT(*) FROM ddl_par_new;
ROLLBACK;

-- A plan cached before the block keeps its Gather when the block's DDL
-- changes nothing the plan depends on. The plan depends on f only, so
-- replacing g, which f calls, does not invalidate it. Workers read the
-- committed catalog and would run the old g. With leader participation off,
-- every row goes through a worker, so a wrong run sums to 1000; the correct
-- sum in the block is 2000. Both the cached plan and the same query planned
-- in the block keep their Gather, and must launch no workers.
SET parallel_leader_participation = off;
CREATE TABLE ddl_par_gap (k INT, PRIMARY KEY (k ASC));
INSERT INTO ddl_par_gap SELECT i FROM generate_series(1, 1000) i;
ANALYZE ddl_par_gap;
CREATE FUNCTION ddl_par_g() RETURNS INT LANGUAGE sql PARALLEL SAFE
  AS 'SELECT 1';
CREATE FUNCTION ddl_par_f(INT) RETURNS INT LANGUAGE plpgsql PARALLEL SAFE
  AS $$ BEGIN RETURN ddl_par_g(); END $$;
PREPARE ddl_par_sum AS SELECT SUM(ddl_par_f(k)) FROM ddl_par_gap;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_sum;
EXECUTE ddl_par_sum;

BEGIN;
CREATE OR REPLACE FUNCTION ddl_par_g() RETURNS INT LANGUAGE sql PARALLEL SAFE
  AS 'SELECT 2';
SELECT ddl_par_g();
EXPLAIN (COSTS OFF) SELECT SUM(ddl_par_f(k)) FROM ddl_par_gap;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
  SELECT SUM(ddl_par_f(k)) FROM ddl_par_gap;
SELECT SUM(ddl_par_f(k)) FROM ddl_par_gap;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_sum;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) EXECUTE ddl_par_sum;
EXECUTE ddl_par_sum;
ROLLBACK;

DEALLOCATE ddl_par_sum;
RESET parallel_leader_participation;
DROP FUNCTION ddl_par_f(INT);
DROP FUNCTION ddl_par_g();
DROP TABLE ddl_par_gap;
RESET force_parallel_mode;
