--
-- PG transaction IDs (xids) and temp tables.  In YB, only writes to local
-- (temp) storage, or an explicit request such as txid_current(), assign a PG
-- xid; DocDB writes and catalog changes never do.  Commit and abort must work
-- whether or not an xid was assigned, and temp storage must be removed on
-- both paths.
--

-- A plan cached during CREATE TEMP TABLE must not make a later DDL fail with
-- "cannot commit a transaction that deleted files but has no xid" (#35117).
CREATE FUNCTION evt_fn() RETURNS event_trigger LANGUAGE plpgsql AS $$
BEGIN
  PERFORM 1;
END $$;
CREATE EVENT TRIGGER evt ON ddl_command_end EXECUTE FUNCTION evt_fn();
CREATE TABLE evt_t (id int, v int);
CREATE INDEX evt_t_v ON evt_t (v);
\c
CREATE TEMP TABLE evt_tt (a int);
DROP INDEX evt_t_v;
SELECT to_regclass('evt_t_v') IS NULL AS index_dropped;
DROP EVENT TRIGGER evt;
DROP FUNCTION evt_fn();
DROP TABLE evt_t;

-- Same, through a saved SPI plan and REFRESH MATERIALIZED VIEW (#35096).
CREATE TABLE mv_base (id int PRIMARY KEY, k int);
INSERT INTO mv_base SELECT g, g % 10 FROM generate_series(1, 10000) g;
CREATE MATERIALIZED VIEW mv_k AS SELECT k, count(*) AS c FROM mv_base GROUP BY k;
CREATE UNIQUE INDEX mv_k_k ON mv_k (k);
CREATE TABLE mv_marker (id int PRIMARY KEY);
\c
BEGIN ISOLATION LEVEL REPEATABLE READ;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_k;
SELECT pg_get_viewdef('mv_k'::regclass, true) IS NOT NULL AS has_viewdef;
COMMIT;
BEGIN;
INSERT INTO mv_marker VALUES (1);
REFRESH MATERIALIZED VIEW mv_k;
SELECT pg_get_viewdef('mv_k'::regclass, true) IS NOT NULL AS has_viewdef;
COMMIT;
SELECT count(*) FROM mv_marker;
DROP MATERIALIZED VIEW mv_k;
DROP TABLE mv_base, mv_marker;

-- Dropping a sequence registers no pending file delete: only temp relations
-- have local storage.
CREATE SEQUENCE seq_drop;
DROP SEQUENCE seq_drop;
BEGIN;
CREATE SEQUENCE seq_drop_txn;
DROP SEQUENCE seq_drop_txn;
COMMIT;
CREATE TABLE serial_drop (k serial PRIMARY KEY);
DROP TABLE serial_drop;
CREATE SCHEMA seq_schema;
CREATE SEQUENCE seq_schema.s;
DROP SCHEMA seq_schema CASCADE;

-- Converting a table to a view drops the table's storage.
CREATE TABLE tbl_to_view (k int);
CREATE RULE "_RETURN" AS ON SELECT TO tbl_to_view DO INSTEAD SELECT 1::int AS k;
SELECT relkind FROM pg_class WHERE relname = 'tbl_to_view';
SELECT * FROM tbl_to_view;
DROP VIEW tbl_to_view;

-- DDL on YB relations drops their stats entries in a transaction with no xid.
CREATE TABLE yb_stats (k int PRIMARY KEY, v int);
CREATE INDEX yb_stats_v ON yb_stats (v);
INSERT INTO yb_stats VALUES (1, 1);
BEGIN;
ALTER TABLE yb_stats ALTER COLUMN v TYPE bigint;
TRUNCATE yb_stats;
DROP INDEX yb_stats_v;
DROP TABLE yb_stats;
COMMIT;
SELECT to_regclass('yb_stats') IS NULL AS table_dropped;

-- A temp write below the top-level statement assigns an xid, and the commit
-- must record it (#27736, #34440).
CREATE TABLE yb_dst (k int PRIMARY KEY, v int);
CREATE TEMP TABLE tmp_src (k int PRIMARY KEY, v int);
WITH cte AS (INSERT INTO tmp_src VALUES (10, 10) RETURNING *)
INSERT INTO yb_dst SELECT * FROM cte;
SELECT * FROM tmp_src;
SELECT * FROM yb_dst;
DROP TABLE yb_dst;

CREATE TEMP TABLE tmp_cte (k int PRIMARY KEY, v int);
INSERT INTO tmp_cte VALUES (1, 1), (2, 2), (3, 3);
WITH d AS (DELETE FROM tmp_cte WHERE k = 1 RETURNING *) SELECT * FROM d;
WITH u AS (UPDATE tmp_cte SET v = 0 WHERE k = 2 RETURNING *) SELECT * FROM u;
BEGIN;
WITH d AS (DELETE FROM tmp_cte WHERE k = 3 RETURNING *) SELECT * FROM d;
COMMIT;
SELECT * FROM tmp_cte ORDER BY k;

-- An explicitly assigned xid is committed or aborted with its transaction
-- (#33128, #36986).
SELECT txid_current() IS NOT NULL AS assigned;
BEGIN;
SELECT pg_current_xact_id()::text AS committed_xid \gset
COMMIT;
BEGIN;
SELECT pg_current_xact_id()::text AS aborted_xid \gset
ROLLBACK;
BEGIN;
SELECT pg_current_xact_id()::text AS savepoint_xid \gset
SAVEPOINT s;
SELECT txid_current() IS NOT NULL AS assigned_in_savepoint;
ROLLBACK TO s;
COMMIT;
SELECT pg_xact_status(:'committed_xid'::xid8) AS committed,
       pg_xact_status(:'aborted_xid'::xid8) AS aborted,
       pg_xact_status(:'savepoint_xid'::xid8) AS with_savepoint;

-- YB: temp DDL that writes no tuples assigns no xid.  PostgreSQL assigns one,
-- because its catalog rows are heap tuples; YB's go to DocDB.  The first tuple
-- written to a temp table assigns one as usual.
BEGIN;
CREATE TEMP TABLE c1 (k int);
SELECT txid_current_if_assigned() IS NULL AS no_xid_after_create;
CREATE INDEX c1_k ON c1 (k);
ALTER TABLE c1 ALTER COLUMN k TYPE bigint;
ALTER TABLE c1 RENAME TO c1r;
TRUNCATE c1r;
SELECT txid_current_if_assigned() IS NULL AS no_xid_after_ddl;
INSERT INTO c1r VALUES (1);
SELECT txid_current_if_assigned() IS NOT NULL AS xid_after_insert;
COMMIT;
SELECT count(*) FROM c1r;
BEGIN;
DROP TABLE c1r;
SELECT txid_current_if_assigned() IS NULL AS no_xid_after_drop;
COMMIT;

-- Temp files are removed by commit and by abort with no xid.
CREATE TEMP TABLE c3 (k int);
SELECT pg_relation_filepath('c3') AS c3_path \gset
SELECT (pg_stat_file(:'c3_path', true)).size IS NOT NULL AS file_exists;
DROP TABLE c3;
SELECT (pg_stat_file(:'c3_path', true)).size IS NULL AS file_removed;

BEGIN;
CREATE TEMP TABLE c4 (k int);
SELECT pg_relation_filepath('c4') AS c4_path \gset
SELECT (pg_stat_file(:'c4_path', true)).size IS NOT NULL AS file_exists;
ROLLBACK;
SELECT (pg_stat_file(:'c4_path', true)).size IS NULL AS file_removed;

-- ON COMMIT actions.
BEGIN;
CREATE TEMP TABLE c5_drop (k int) ON COMMIT DROP;
COMMIT;
SELECT to_regclass('pg_temp.c5_drop') IS NULL AS dropped_on_commit;
CREATE TEMP TABLE c5_rows (k int) ON COMMIT DELETE ROWS;
BEGIN;
INSERT INTO c5_rows VALUES (1);
SELECT count(*) FROM c5_rows;
COMMIT;
SELECT count(*) FROM c5_rows;
