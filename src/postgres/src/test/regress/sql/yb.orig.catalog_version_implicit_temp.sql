-- Test that relations that are temporary without the TEMP keyword do not
-- increment the catalog version, and that DDLs on temp relations that can
-- reach permanent objects do
\set template1_db_oid 1
\set db_oid 'CASE WHEN (select count(*) from pg_yb_catalog_version) = 1 THEN :template1_db_oid ELSE (SELECT oid FROM pg_database WHERE datname = \'yugabyte\') END'
\set reset_baseline 'UPDATE baseline_version SET current_version = (SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = :db_oid)'
\set version_unchanged 'SELECT current_version = (SELECT current_version FROM baseline_version) AS version_unchanged FROM pg_yb_catalog_version WHERE db_oid = :db_oid'

CREATE TEMP TABLE tmp_src (a int);
CREATE TEMP TABLE baseline_version AS SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = :db_oid;

-- A view whose query uses a temp relation
CREATE VIEW implicit_view AS SELECT a FROM tmp_src;
CREATE OR REPLACE VIEW implicit_view AS SELECT a FROM tmp_src;
CREATE VIEW implicit_subquery_view AS SELECT a FROM (SELECT a FROM pg_temp.tmp_src) s;
-- Relations created in the temp schema
CREATE OR REPLACE VIEW pg_temp.implicit_view AS SELECT a FROM tmp_src;
CREATE VIEW pg_temp.explicit_view AS SELECT 1 AS a;
CREATE TABLE pg_temp.explicit_tbl (a int);
CREATE TABLE pg_temp.explicit_ctas AS SELECT 1 AS a;
CREATE SEQUENCE pg_temp.explicit_seq;
:version_unchanged;
SELECT relname, relpersistence FROM pg_class
WHERE relname IN ('implicit_view', 'implicit_subquery_view', 'explicit_view',
				  'explicit_tbl', 'explicit_ctas', 'explicit_seq')
ORDER BY relname;

-- The CTE hides the temp table of the same name, so the view is permanent
:reset_baseline;
CREATE VIEW cte_view AS WITH tmp_src AS (SELECT 1 AS a) SELECT a FROM tmp_src;
:version_unchanged;
SELECT relpersistence FROM pg_class WHERE relname = 'cte_view';
DROP VIEW cte_view;

-- A temp table that inherits from a permanent one changes the permanent one
CREATE TABLE perm_parent (a int);
:reset_baseline;
CREATE TEMP TABLE tmp_child () INHERITS (perm_parent);
:version_unchanged;
DROP TABLE tmp_child;
DROP TABLE perm_parent;

-- A permanent object can depend on a temp view or sequence, so a CASCADE drop
-- of one increments the catalog version, and a plain drop does not
CREATE TEMP VIEW tmp_drop_view AS SELECT 1 AS a;
CREATE TEMP VIEW tmp_cascade_view AS SELECT 1 AS a;
CREATE TEMP SEQUENCE tmp_drop_seq;
CREATE TEMP SEQUENCE tmp_cascade_seq;
:reset_baseline;
DROP VIEW tmp_drop_view;
DROP SEQUENCE tmp_drop_seq;
:version_unchanged;
DROP VIEW tmp_cascade_view CASCADE;
:version_unchanged;
:reset_baseline;
DROP SEQUENCE tmp_cascade_seq CASCADE;
:version_unchanged;
