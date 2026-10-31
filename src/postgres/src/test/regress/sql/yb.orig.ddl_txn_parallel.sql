--
-- GH #30978: a parallel worker reads only committed catalog rows, so a
-- query after a DDL in the same transaction block gets no parallel plan.
-- Scenario: BEGIN, ALTER TABLE ADD COLUMN c, then a scan that filters on c.
-- A worker launched there fails with "invalid attnum 3 for relation".
--
SET yb_enable_cbo = on;
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
SELECT COUNT(*) FROM ddl_par WHERE c = 'g';
EXPLAIN (COSTS OFF) EXECUTE ddl_par_count;
EXECUTE ddl_par_count;
ROLLBACK;

DEALLOCATE ddl_par_count;
RESET plan_cache_mode;

DROP TABLE ddl_par;

-- GH #34740: a parallel scan of a table created earlier in the same
-- transaction block must not fail with "relation with OID ... does not
-- exist".
SET force_parallel_mode = on;
BEGIN;
CREATE TABLE ddl_par_new (k INT);
INSERT INTO ddl_par_new VALUES (1), (2);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM ddl_par_new;
SELECT COUNT(*) FROM ddl_par_new;
ROLLBACK;
RESET force_parallel_mode;
