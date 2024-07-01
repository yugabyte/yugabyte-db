--
-- YB REINDEX test
--

--
-- PART 0: Init
--
SET yb_enable_create_with_table_oid TO on;
CREATE TABLE yb (i int PRIMARY KEY, j int) WITH (table_oid = 54321);
CREATE INDEX NONCONCURRENTLY ON yb (j);

UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'pg_class_oid_index'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'pg_depend_depender_index'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
SELECT oid AS db_oid FROM pg_database WHERE datname = (
    SELECT CASE
        WHEN COUNT(*) = 1 THEN 'template1'
        ELSE current_database() END FROM pg_yb_catalog_version) \gset
SELECT
$force_cache_refresh$
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_yb_catalog_version
   SET current_version       = current_version + 1,
       last_breaking_version = current_version + 1
 WHERE db_oid = :db_oid;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
DO
$$
BEGIN
    PERFORM pg_sleep(5);
END;
$$;
$force_cache_refresh$ AS force_cache_refresh \gset
-- Force cache refresh.  The UPDATE pg_yb_catalog_version trick doesn't seem to
-- work for pg_depend_depender_index, but updating the pg_yb_catalog_version
-- and then reconnecting does (simply reconnecting doesn't result in a cache
-- refresh when the tserver response cache is enabled). Since reconnecting
-- releases temp tables, do this refresh first before creating temp tables.
:force_cache_refresh
\c

CREATE TEMP TABLE tmp (i int PRIMARY KEY, j int);
CREATE INDEX ON tmp (j);

SELECT relname FROM pg_class c JOIN pg_index i on c.oid = i.indexrelid
    WHERE indisvalid = false;

--
-- PART 1: simple REINDEX on system catalog, temporary, and yb objects
--

-- REINDEX SYSTEM/DATABASE expects current database name.
SELECT current_database();
REINDEX SYSTEM yugabyte; -- current database
REINDEX SYSTEM template0; -- different database
REINDEX DATABASE yugabyte; -- current database
REINDEX DATABASE template0; -- different database
REINDEX SCHEMA pg_catalog;
REINDEX SCHEMA pg_temp;
REINDEX SCHEMA public;
REINDEX TABLE pg_class; -- system table with pk
REINDEX TABLE pg_depend; -- system table without pk
REINDEX TABLE tmp;
REINDEX TABLE yb;
REINDEX INDEX pg_class_oid_index; -- system pk index
REINDEX INDEX pg_depend_depender_index; -- system secondary index
REINDEX INDEX tmp_pkey;
REINDEX INDEX tmp_j_idx;
REINDEX INDEX yb_pkey;
REINDEX INDEX yb_j_idx;

-- Any rebuilt indexes should now be public.
SELECT relname FROM pg_class c JOIN pg_index i on c.oid = i.indexrelid
    WHERE indisvalid = false;

--
-- PART 2: scans before/after REINDEX
--

INSERT INTO yb SELECT g, -g FROM generate_series(1, 10) g;
INSERT INTO tmp SELECT g, -g FROM generate_series(1, 10) g;

-- 1. initial state
EXPLAIN (costs off)
/*+SeqScan(pg_depend) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+SeqScan(pg_depend) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
EXPLAIN (costs off)
/*+IndexScan(pg_depend_depender_index) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+IndexScan(pg_depend_depender_index) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
EXPLAIN (costs off)
/*+SeqScan(tmp) */
SELECT i FROM tmp WHERE j = -5;
/*+SeqScan(tmp) */
SELECT i FROM tmp WHERE j = -5;
SET enable_bitmapscan = on;
EXPLAIN (costs off)
/*+IndexScan(tmp_j_idx) */
SELECT i FROM tmp WHERE j = -5;
/*+IndexScan(tmp_j_idx) */
SELECT i FROM tmp WHERE j = -5;
RESET enable_bitmapscan;
EXPLAIN (costs off)
/*+SeqScan(yb) */
SELECT i FROM yb WHERE j = -5;
/*+SeqScan(yb) */
SELECT i FROM yb WHERE j = -5;
EXPLAIN (costs off)
/*+IndexScan(yb_j_idx) */
SELECT i FROM yb WHERE j = -5;
/*+IndexScan(yb_j_idx) */
SELECT i FROM yb WHERE j = -5;

-- 2. corruption (for temp index)
--
-- Fully test the temp index first because system indexes require require a
-- reconnect, which releases the temp table.
--
-- Disable reads/writes to the index.
UPDATE pg_index SET indislive = false, indisready = false, indisvalid = false
    WHERE indexrelid = 'tmp_j_idx'::regclass;
--- Force cache refresh.
:force_cache_refresh
-- Do update that goes to table but doesn't go to index.
UPDATE tmp SET i = 11 WHERE j = -5;
-- Enable reads/writes to the index.
UPDATE pg_index SET indislive = true, indisready = true, indisvalid = true
    WHERE indexrelid = 'tmp_j_idx'::regclass;
--- Force cache refresh.
:force_cache_refresh
-- Show the corruption.
/*+SeqScan(tmp) */
SELECT i FROM tmp WHERE j = -5;
/*+IndexScan(tmp_j_idx) */
SELECT i FROM tmp WHERE j = -5;
-- Disable reads/writes to the index.
UPDATE pg_index SET indislive = false, indisready = false, indisvalid = false
    WHERE indexrelid = 'tmp_j_idx'::regclass;
--- Force cache refresh.
:force_cache_refresh

-- 3. reindex (for temp index)
REINDEX INDEX tmp_j_idx;

-- 4. verification (for temp index)
EXPLAIN (costs off)
/*+SeqScan(tmp) */
SELECT i FROM tmp WHERE j = -5;
/*+SeqScan(tmp) */
SELECT i FROM tmp WHERE j = -5;
-- Somehow, IndexScan hint plan fails to work at this point.  Force usage of
-- the index with enable_seqscan.
SET enable_seqscan TO off;
EXPLAIN (costs off)
SELECT i FROM tmp WHERE j = -5;
SELECT i FROM tmp WHERE j = -5;
RESET enable_seqscan;

-- 5. corruption (for YB indexes)
--
-- Now, test the YB-backed indexes.
--
-- Disable reads/writes to the indexes.
UPDATE pg_index SET indislive = false, indisready = false, indisvalid = false
    WHERE indexrelid = 'pg_depend_depender_index'::regclass;
UPDATE pg_index SET indislive = false, indisready = false, indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
-- Force cache refresh.
:force_cache_refresh
\c
-- Do updates that go to tables but don't go to indexes.
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
-- For non-pk tables, the ybctid cannot be changed through UPDATE.  Use the
-- following DELETE + INSERT instead.
WITH w AS (
    DELETE FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass
    RETURNING *)
INSERT INTO pg_depend SELECT * FROM w;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
UPDATE yb SET i = 11 WHERE j = -5;
-- Enable reads/writes to the indexes.
UPDATE pg_index SET indislive = true, indisready = true, indisvalid = true
    WHERE indexrelid = 'pg_depend_depender_index'::regclass;
UPDATE pg_index SET indislive = true, indisready = true, indisvalid = true
    WHERE indexrelid = 'yb_j_idx'::regclass;
-- Force cache refresh.
:force_cache_refresh
\c
-- Show the corruptions.
/*+SeqScan(pg_depend) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+IndexScan(pg_depend_depender_index) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+SeqScan(yb) */
SELECT i FROM yb WHERE j = -5;
/*+IndexScan(yb_j_idx) */
SELECT i FROM yb WHERE j = -5;
-- Disable reads to the indexes.
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'pg_depend_depender_index'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
-- Force cache refresh.
:force_cache_refresh
\c
-- 6. reindex (for YB indexes)
REINDEX INDEX pg_depend_depender_index;
REINDEX INDEX yb_j_idx;

-- 7. verification (for YB indexes)
EXPLAIN (costs off)
/*+SeqScan(pg_depend) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+SeqScan(pg_depend) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
EXPLAIN (costs off)
/*+IndexScan(pg_depend_depender_index) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
/*+IndexScan(pg_depend_depender_index) */
SELECT deptype FROM pg_depend
    WHERE classid = 'pg_class'::regclass and objid = 'yb'::regclass;
EXPLAIN (costs off)
/*+SeqScan(yb) */
SELECT i FROM yb WHERE j = -5;
/*+SeqScan(yb) */
SELECT i FROM yb WHERE j = -5;
EXPLAIN (costs off)
/*+IndexScan(yb_j_idx) */
SELECT i FROM yb WHERE j = -5;
/*+IndexScan(yb_j_idx) */
SELECT i FROM yb WHERE j = -5;

--
-- PART 3: misc
--

-- public index
REINDEX INDEX pg_depend_depender_index;
REINDEX INDEX yb_j_idx;

-- VERBOSE option
\set VERBOSITY terse
REINDEX (VERBOSE) SYSTEM yugabyte;
REINDEX (VERBOSE) DATABASE yugabyte;
REINDEX (VERBOSE) SCHEMA public;
REINDEX (VERBOSE) TABLE yb;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
\c
REINDEX (VERBOSE) INDEX yb_j_idx;
\set VERBOSITY default

-- unsupported command/options
REINDEX INDEX CONCURRENTLY yb_j_idx;
REINDEX (VERBOSE) INDEX CONCURRENTLY yb_j_idx;
REINDEX (CONCURRENTLY) INDEX yb_j_idx;
REINDEX (tablespace somespace) INDEX yb_j_idx;

-- shared system index
REINDEX INDEX pg_database_datname_index; -- fail
-- make sure index isn't broken after failure
/*+IndexOnlyScan(pg_database pg_database_datname_index)*/
SELECT datname from pg_database WHERE datname LIKE 'template%';

-- colocation (via tablegroup)
CREATE TABLEGROUP g;
CREATE TABLE ing (i int PRIMARY KEY, j int) TABLEGROUP g;
CREATE INDEX NONCONCURRENTLY ON ing (j ASC);
INSERT INTO ing SELECT g, -g FROM generate_series(1, 10) g;
EXPLAIN (costs off)
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j < -8 ORDER BY i;
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j < -8 ORDER BY i;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'ing_j_idx'::regclass;
\c
REINDEX INDEX ing_j_idx;
EXPLAIN (costs off)
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j < -8 ORDER BY i;
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j < -8 ORDER BY i;
EXPLAIN (costs off)
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j = -9;
/*+IndexScan(ing_j_idx)*/
SELECT i FROM ing WHERE j = -9;
DROP TABLE ing;

-- matview
CREATE MATERIALIZED VIEW mv AS SELECT * FROM yb;
CREATE INDEX NONCONCURRENTLY ON mv (j ASC);
EXPLAIN (costs off)
/*+IndexScan(mv_j_idx)*/
SELECT i FROM mv WHERE j > -3 ORDER BY i;
/*+IndexScan(mv_j_idx)*/
SELECT i FROM mv WHERE j > -3 ORDER BY i;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'mv_j_idx'::regclass;
\c
REINDEX INDEX mv_j_idx;
EXPLAIN (costs off)
/*+IndexScan(mv_j_idx)*/
SELECT i FROM mv WHERE j > -3 ORDER BY i;
/*+IndexScan(mv_j_idx)*/
SELECT i FROM mv WHERE j > -3 ORDER BY i;
DROP MATERIALIZED VIEW mv;

-- partitioned table
CREATE TABLE parted (i int, j int) PARTITION BY LIST (j);
CREATE INDEX NONCONCURRENTLY ON parted (i);
CREATE TABLE parted_odd PARTITION OF parted FOR VALUES IN (1, 3, 5, 7, 9);
CREATE TABLE parted_even PARTITION OF parted FOR VALUES IN (2, 4, 6, 8);
INSERT INTO parted SELECT (2 * g), g FROM generate_series(1, 9) g;
EXPLAIN (costs off)
/*+IndexOnlyScan(parted_i_idx)*/
SELECT i FROM parted WHERE i = (2 * 5);
/*+IndexOnlyScan(parted_i_idx)*/
SELECT i FROM parted WHERE i = (2 * 5);
EXPLAIN (costs off)
/*+IndexOnlyScan(parted_odd_i_idx)*/
SELECT i FROM parted_odd WHERE i = (2 * 5);
/*+IndexOnlyScan(parted_odd_i_idx)*/
SELECT i FROM parted_odd WHERE i = (2 * 5);
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'parted_i_idx'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'parted_odd_i_idx'::regclass;
\c
REINDEX INDEX parted_i_idx;
REINDEX INDEX parted_odd_i_idx;
EXPLAIN (costs off)
/*+IndexOnlyScan(parted_i_idx)*/
SELECT i FROM parted WHERE i = (2 * 5);
/*+IndexOnlyScan(parted_i_idx)*/
SELECT i FROM parted WHERE i = (2 * 5);
EXPLAIN (costs off)
/*+IndexOnlyScan(parted_odd_i_idx)*/
SELECT i FROM parted_odd WHERE i = (2 * 5);
/*+IndexOnlyScan(parted_odd_i_idx)*/
SELECT i FROM parted_odd WHERE i = (2 * 5);
DROP TABLE parted;

--
-- PART -1: cleanup
--

DROP TABLE yb;
