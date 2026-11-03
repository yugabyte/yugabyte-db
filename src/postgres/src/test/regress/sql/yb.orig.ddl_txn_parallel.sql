--
-- GH #30978: a parallel worker reads only committed catalog rows, so a
-- query after a DDL in the same transaction block gets no parallel plan.
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

-- A generic plan cached before the block is parallel; the ALTER TABLE
-- invalidates it, and the replanned query has no Gather.
SET plan_cache_mode = force_generic_plan;
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
RESET plan_cache_mode;

-- Once the block ends, queries get parallel plans again, whether the block
-- rolled back (above) or committed.
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par;
BEGIN;
CREATE TABLE ddl_par_commit (k INT);
COMMIT;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par;
DROP TABLE ddl_par_commit;

-- A generic plan first built after the DDL has no Gather. It depends on
-- nothing the block changed, so once the block ends it must still be
-- replanned, or it stays serial for the rest of the session.
BEGIN;
CREATE TABLE ddl_par_after_t (k INT);
PREPARE ddl_par_after AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_after;
COMMIT;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_after;
DEALLOCATE ddl_par_after;
DROP TABLE ddl_par_after_t;

-- GH #30978 as filed ran in REPEATABLE READ and rolled back to a savepoint
-- taken after the DDL. The DDL before the savepoint stays, so the block
-- still has uncommitted catalog rows and the query gets no parallel plan.
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

-- A temp-table DDL also turns off parallel query for the rest of the block,
-- even for queries on permanent tables, although workers never read temp
-- tables. TODO(#ISSUE): only non-temp DDL should do this. This block also
-- skips the catalog version bump, so a generic plan made in it must still be
-- replanned with a Gather after COMMIT.
BEGIN;
CREATE TEMP TABLE ddl_par_tmp (k INT);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par;
PREPARE ddl_par_tmp_q AS SELECT COUNT(*) FROM ddl_par;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_tmp_q;
COMMIT;
EXPLAIN (COSTS OFF) EXECUTE ddl_par_tmp_q;
DEALLOCATE ddl_par_tmp_q;
DROP TABLE ddl_par_tmp;

-- CREATE TABLE AS is a DDL, so it sets the flag before it plans its query.
-- With transactional DDL on, yb_disable_parallel_query_in_ddl = off then
-- brings no parallel plan back.
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
-- sum in the block is 2000. The same query planned in the block has no
-- Gather. The cached plan keeps its Gather, but must launch no workers.
SET plan_cache_mode = force_generic_plan;
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
RESET plan_cache_mode;
DROP FUNCTION ddl_par_f(INT);
DROP FUNCTION ddl_par_g();
DROP TABLE ddl_par_gap;
RESET force_parallel_mode;
