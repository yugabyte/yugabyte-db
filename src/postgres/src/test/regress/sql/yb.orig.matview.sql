--
-- YB tests for materialized views
--
CREATE TABLE t_simple (k INT PRIMARY KEY);
INSERT INTO t_simple (SELECT i FROM generate_series(1, 10) AS i);

-- GH-26205: Test that PostgreSQL XID increases by 1 for a concurrent refresh
-- and remains the same for a non-concurrent refresh. Placing this at the top
-- of the file to ensure that this is close to being the first set of commands
-- that are executed upon cluster creation and there is no variability in the
-- XIDs generated.
CREATE MATERIALIZED VIEW mv_xid_test AS (SELECT * FROM t_simple);
CREATE UNIQUE INDEX ON mv_xid_test (k);
SELECT age('3'::xid);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
SELECT age('3'::xid);
REFRESH MATERIALIZED VIEW mv_xid_test;
SELECT age('3'::xid);
-- A few variations of the above test.
BEGIN ISOLATION LEVEL REPEATABLE READ;
REFRESH MATERIALIZED VIEW mv_xid_test;
INSERT INTO t_simple VALUES (11), (12);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
SELECT COUNT(*) FROM mv_xid_test;
COMMIT;
SELECT age('3'::xid);

BEGIN ISOLATION LEVEL READ COMMITTED;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
REFRESH MATERIALIZED VIEW mv_xid_test;
INSERT INTO t_simple VALUES (13), (14);
SELECT COUNT(*) FROM mv_xid_test;
COMMIT;
-- The XID should jump by 2 in Read Committed mode as the concurrent refresh of
-- the matview is run as a sub-transaction. So, an additional XID is assigned to
-- its parent. In release builds where Read Committed isolation is disabled, the
-- XID should increase by 1.
SELECT age('3'::xid);

CREATE TABLE test_yb (col int);
INSERT INTO test_yb VALUES (null);
CREATE UNLOGGED MATERIALIZED VIEW unlogged_mv_yb AS SELECT * FROM test_yb; -- not supported
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb;
CREATE UNIQUE INDEX ON mtest_yb(col);
REFRESH MATERIALIZED VIEW NONCONCURRENTLY mtest_yb;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb; -- should fail
CREATE TABLE pg_temp_foo (col int);
INSERT INTO pg_temp_foo values (1);
SELECT * FROM pg_temp_foo;
CREATE TABLE pg_temp__123 (col int);
INSERT INTO pg_temp__123 values (1);
SELECT * from pg_temp__123;
DROP TABLE test_yb CASCADE;

-- Alter materialized view - rename matview and rename columns
CREATE TABLE test_yb (id int NOT NULL PRIMARY KEY, type text NOT NULL, val numeric NOT NULL);
INSERT INTO test_yb VALUES (1, 'xyz', 2);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb;
CREATE UNIQUE INDEX unique_IDX ON mtest_YB(id);
ALTER MATERIALIZED VIEW mtest_yb RENAME TO mtest_yb1;
SELECT * FROM mtest_yb; -- error
SELECT * from mtest_yb1; -- ok
REFRESH MATERIALIZED VIEW mtest_yb1;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb1;
ALTER MATERIALIZED VIEW mtest_yb1 RENAME TO mtest_yb2;
SELECT * from mtest_yb2;
REFRESH MATERIALIZED VIEW mtest_yb2;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb2;
ALTER MATERIALIZED VIEW mtest_yb2 RENAME val TO total; -- test Alter Rename Column
SELECT * FROM mtest_yb2;
DROP TABLE test_yb CASCADE;

-- Alter materialized view - change owner
CREATE TABLE test_yb (id int NOT NULL PRIMARY KEY, type text NOT NULL, val numeric NOT NULL);
INSERT INTO test_yb VALUES (1, 'xyz', 2);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb;
CREATE UNIQUE INDEX unique_IDX ON mtest_yb(id);
CREATE ROLE test_mv_user;
SET ROLE test_mv_user;
REFRESH MATERIALIZED VIEW mtest_yb; -- error
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb; -- error
SET ROLE yugabyte;
ALTER MATERIALIZED VIEW mtest_yb OWNER TO test_mv_user;
REFRESH MATERIALIZED VIEW mtest_yb; -- error
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb; -- error
ALTER TABLE test_yb OWNER TO test_mv_user;
REFRESH MATERIALIZED VIEW mtest_yb; -- ok
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb; -- ok
ALTER MATERIALIZED VIEW mtest_yb OWNER TO SESSION_USER;
ALTER TABLE test_yb OWNER TO SESSION_USER;
REFRESH MATERIALIZED VIEW mtest_yb; -- ok
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb; -- ok
ALTER MATERIALIZED VIEW mtest_yb RENAME val TO amt;
ALTER MATERIALIZED VIEW mtest_yb RENAME TO mtest_yb1;
CREATE ROLE test_mv_superuser SUPERUSER;
ALTER MATERIALIZED VIEW mtest_yb1 OWNER TO test_mv_superuser;
REFRESH MATERIALIZED VIEW mtest_yb1; -- ok
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb1; -- ok
ALTER MATERIALIZED VIEW mtest_yb1 OWNER TO CURRENT_USER;
DROP ROLE test_mv_user;
DROP ROLE test_mv_superuser;
DROP TABLE test_yb CASCADE;

-- Test special characters in an attribute's name
CREATE TABLE test_yb ("xyzID''\\b" int NOT NULL, "y" int);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb WITH NO DATA;
CREATE UNIQUE INDEX ON mtest_yb("xyzID''\\b");
REFRESH MATERIALIZED VIEW mtest_yb;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb;
DROP TABLE test_yb CASCADE;

-- Test with special characters in the base relation's name
CREATE TABLE "test_YB''\\b" ("xyzid" int NOT NULL);
INSERT INTO "test_YB''\\b" VALUES (1);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM "test_YB''\\b" WITH NO DATA;
CREATE UNIQUE INDEX ON mtest_yb("xyzid");
REFRESH MATERIALIZED VIEW mtest_yb;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb;
DROP TABLE "test_YB''\\b" CASCADE;

-- Test with special characters in the matview's name
CREATE TABLE test_yb ("xyzid" int NOT NULL);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW "mtest_YB''\\b" AS SELECT * FROM test_yb WITH NO DATA;
CREATE UNIQUE INDEX ON mtest_YB("xyzid");
REFRESH MATERIALIZED VIEW mtest_YB;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_YB;
DROP TABLE test_yb CASCADE;

-- Test with special characters in the unique index's name
CREATE TABLE test_yb ("xyzid" int NOT NULL);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb WITH NO DATA;
CREATE UNIQUE INDEX "unique_IDX''\\b" ON mtest_YB("xyzid");
REFRESH MATERIALIZED VIEW mtest_yb;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb;
DROP TABLE test_yb CASCADE;

-- Test with unicode characters
CREATE TABLE test_yb ("U&'\0022hi\0022'" int NOT NULL);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW mtest_yb AS SELECT * FROM test_yb WITH NO DATA;
CREATE UNIQUE INDEX unique_IDX ON mtest_YB("U&'\0022hi\0022'");
REFRESH MATERIALIZED VIEW mtest_yb;
REFRESH MATERIALIZED VIEW CONCURRENTLY mtest_yb;
DROP TABLE test_yb CASCADE;

-- Test with unicode characters from table
CREATE TABLE test_yb ("xyzid" int NOT NULL);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW "mtest_YB''\\b" AS SELECT * FROM test_yb WITH NO DATA;
CREATE UNIQUE INDEX ON "mtest_YB''\\b"("xyzid");
REFRESH MATERIALIZED VIEW "mtest_YB''\\b";
REFRESH MATERIALIZED VIEW CONCURRENTLY "mtest_YB''\\b";
DROP TABLE test_yb CASCADE;

-- Materialized view of a materialized view
CREATE TABLE test_yb ("xyzid" int NOT NULL);
INSERT INTO test_yb VALUES (1);
CREATE MATERIALIZED VIEW "mtest_YB''\\b" AS SELECT * FROM test_yb WITH NO DATA;
CREATE MATERIALIZED VIEW "mtest_YB''\\b\\b" AS SELECT * FROM "mtest_YB''\\b" WITH NO DATA;
CREATE UNIQUE INDEX ON "mtest_YB''\\b\\b"("xyzid");
REFRESH MATERIALIZED VIEW "mtest_YB''\\b";
REFRESH MATERIALIZED VIEW "mtest_YB''\\b\\b";
REFRESH MATERIALIZED VIEW CONCURRENTLY "mtest_YB''\\b\\b";
DROP TABLE test_yb CASCADE;

-- Materialized view of a regular view
CREATE TABLE mvtest_t (id int NOT NULL PRIMARY KEY, type text NOT NULL, amt numeric NOT NULL);
CREATE VIEW mvtest_tv AS SELECT type, sum(amt) AS totamt FROM mvtest_t GROUP BY type;
CREATE MATERIALIZED VIEW mvtest_tm2 AS SELECT * FROM mvtest_tv;
SELECT * FROM mvtest_tm2;
DROP VIEW mvtest_tv CASCADE;

-- SELECT FOR SHARE on Materialized view
CREATE MATERIALIZED VIEW mvtest_tm AS SELECT type, sum(amt) AS totamt FROM mvtest_t GROUP BY type;
REFRESH MATERIALIZED VIEW mvtest_tm WITH NO DATA;
SELECT * FROM mvtest_tm FOR SHARE;
DROP TABLE mvtest_t CASCADE;

-- Materialized view with GIN Indexes
create table mvtest_t3 (id int NOT NULL PRIMARY KEY, a integer[] not null);
INSERT INTO mvtest_t3 VALUES
(1, ARRAY[1, 2, 3, 4, 5]),
(2, ARRAY[1, 2, 3, 4, 5]),
(3, ARRAY[1, 2, 3, 4, 5]),
(4, ARRAY[1, 2, 3, 4, 5]);
create index on mvtest_t3 using ybgin (a);
CREATE MATERIALIZED VIEW mvtest_tv5 AS SELECT a[1], sum(id) FROM mvtest_t3 GROUP BY a[1];
select * from mvtest_tv5;
CREATE MATERIALIZED VIEW mvtest_tv6 AS SELECT * FROM mvtest_t3;
create index on mvtest_tv6 using ybgin (a);
select a[1] from mvtest_tv6;

CREATE TABLE arrays (a int[], k serial PRIMARY KEY);
CREATE INDEX NONCONCURRENTLY ON arrays USING ybgin (a);
INSERT INTO arrays VALUES
('{1,1,6}'),
('{1,6,1}'),
('{2,3,6}'),
('{2,5,8}'),
('{null}'),
('{}'),
(null);
INSERT INTO arrays SELECT '{0}' FROM generate_series(1, 1000);
CREATE MATERIALIZED VIEW mvtest_tv7 AS SELECT * FROM arrays;
explain (costs off) select * from mvtest_tv7 where a @> '{6}';
CREATE INDEX NONCONCURRENTLY ON mvtest_tv7 using ybgin (a);
explain (costs off) select * from mvtest_tv7 where a @> '{6}';
select * from mvtest_tv7 where a @> '{6}' order by k;
INSERT INTO arrays SELECT '{0}' FROM generate_series(1, 1000);
INSERT INTO arrays VALUES
('{6,6,6}'),
('{7,6,7}');
refresh materialized view mvtest_tv7;
select * from mvtest_tv7 where a @> '{6}' order by k;

-- Materialized view with Collation
CREATE TABLE collate_test_POSIX (
    a int,
    b text COLLATE "POSIX" NOT NULL
);
CREATE MATERIALIZED VIEW mv_collate_test_POSIX AS SELECT * FROM collate_test_POSIX;
INSERT INTO collate_test_POSIX VALUES (1, 'abc'), (2, 'Abc'), (3, 'bbc'), (4, 'ABD'), (5, 'zzz'), (6, 'ZZZ');
REFRESH MATERIALIZED VIEW mv_collate_test_POSIX;
SELECT * FROM mv_collate_test_POSIX ORDER BY b;
SELECT * FROM mv_collate_test_POSIX ORDER BY b COLLATE "en-US-x-icu";
CREATE MATERIALIZED VIEW mv_collate_test_explicit_collation AS SELECT b COLLATE "en-US-x-icu" FROM collate_test_POSIX;
SELECT * FROM mv_collate_test_explicit_collation ORDER BY b;

-- Test EXPLAIN ANALYZE + CREATE MATERIALIZED VIEW AS.
-- Use EXECUTE to hide the output since it won't be stable.
DO $$
BEGIN
  EXECUTE 'EXPLAIN ANALYZE CREATE MATERIALIZED VIEW view_as_1 AS SELECT 1';
END$$;

SELECT * FROM view_as_1;

-- Colocated materialized view
CREATE DATABASE mydb WITH colocation = true;
\c mydb;
CREATE TABLE base (col int);
CREATE MATERIALIZED VIEW mv AS SELECT * FROM base;
CREATE UNIQUE INDEX mv_idx ON mv(col);
SELECT * FROM mv;
INSERT INTO base VALUES (1);
REFRESH MATERIALIZED VIEW mv;
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('mv'::regclass);
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('mv_idx'::regclass);
SELECT * FROM mv;
INSERT INTO base VALUES (2);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv;
SELECT * FROM mv ORDER BY col;
DROP MATERIALIZED VIEW mv;
SELECT * FROM mv; -- should fail.
CREATE MATERIALIZED VIEW mv WITH (COLOCATION=false) AS SELECT * FROM base;
CREATE UNIQUE INDEX mv_idx ON mv(col);
SELECT * FROM mv ORDER BY col;
INSERT INTO base VALUES (3);
REFRESH MATERIALIZED VIEW mv;
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('mv'::regclass);
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('mv_idx'::regclass);
SELECT * FROM mv ORDER BY col;
INSERT INTO base VALUES (4);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv;
SELECT * FROM mv ORDER BY col;
DROP MATERIALIZED VIEW mv;
SELECT * FROM mv; -- should fail.

-- Tablegroup materialized view
CREATE DATABASE testdb;
\c testdb;
CREATE TABLEGROUP test_tg;
CREATE TABLE test_t (col int) TABLEGROUP test_tg;
CREATE MATERIALIZED VIEW mv AS SELECT * FROM test_t;
SELECT * FROM mv;
INSERT INTO test_t VALUES (1);
REFRESH MATERIALIZED VIEW mv;
SELECT * FROM mv;
INSERT INTO test_t VALUES (2);
CREATE UNIQUE INDEX ON mv(col);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv;
SELECT * FROM mv ORDER BY col;
DROP MATERIALIZED VIEW mv;
SELECT * FROM mv;

-- Split options on indexes should not be copied after a non-concurrent refresh
-- on a matview.
\c yugabyte;
CREATE TABLE test_yb(t int, j int);
CREATE MATERIALIZED VIEW mv AS SELECT * FROM test_yb;
CREATE INDEX idx ON mv(t) SPLIT INTO 5 TABLETS;
REFRESH MATERIALIZED VIEW mv;
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('idx'::regclass);

-- Matview with tablespace
DROP MATERIALIZED VIEW mv;
CREATE TABLESPACE mv_tblspace1 LOCATION '/data';
CREATE TABLESPACE mv_tblspace2 LOCATION '/data';
CREATE MATERIALIZED VIEW mv TABLESPACE mv_tblspace1 AS SELECT * FROM test_yb;
\d mv;
ALTER MATERIALIZED VIEW mv SET TABLESPACE mv_tblspace2;
\d mv;

-- Matview with temp tables + transactions
CREATE TEMPORARY TABLE t_temp (a INT, b INT);
BEGIN;
INSERT INTO t_temp VALUES (1, 1), (2, 2);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
SELECT COUNT(*) FROM t_temp;
COMMIT;
SELECT COUNT(*) FROM t_temp;

-- Rolling back operations on temp tables should also roll back any
-- transactional side effects of the corresponding postgres transaction.
BEGIN;
SAVEPOINT sp1;
INSERT INTO t_temp VALUES (3, 3), (4, 4);
SAVEPOINT sp2;
ROLLBACK TO sp1;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
COMMIT;
SELECT COUNT(*) FROM t_temp;

BEGIN;
SAVEPOINT sp1;
REFRESH MATERIALIZED VIEW mv_xid_test;
SAVEPOINT sp2;
INSERT INTO t_temp VALUES (3, 3), (4, 4);
ROLLBACK TO sp2;
COMMIT;
SELECT COUNT(*) FROM t_temp;

-- Matview with prepared statements + temp tables
PREPARE temp_stmt AS INSERT INTO t_temp VALUES (5, 5), (6, 6);
BEGIN;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_xid_test;
SAVEPOINT sp1;
EXECUTE temp_stmt;
ROLLBACK TO sp1;
EXECUTE temp_stmt;
COMMIT;
SELECT COUNT(*) FROM t_temp;

-- Concurrent refresh on an empty matview
CREATE TABLE base_empty (t int);
CREATE MATERIALIZED VIEW mv_empty AS SELECT * FROM base_empty;
CREATE UNIQUE INDEX ON mv_empty(t);
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_empty;
SELECT * FROM mv_empty;

-- Concurrent refresh with a json column (no equality operator).  Deletes must
-- be matched by unique-index keys, not whole-row equality.
DROP MATERIALIZED VIEW IF EXISTS mv_json;
DROP TABLE IF EXISTS t_json;

CREATE TABLE t_json (
  id text PRIMARY KEY,
  name text,
  info json,
  age int
);
INSERT INTO t_json (id, name, info, age) VALUES
  ('user-1', 'a', '{"x": 1}', 10),
  ('user-2', 'b', '{"x": 2}', 20),
  ('user-3', 'c', '{"x": 3}', 30);

CREATE MATERIALIZED VIEW mv_json AS
  SELECT * FROM t_json
  WITH DATA;

CREATE UNIQUE INDEX uidx_mv_json_id ON mv_json (id);

REFRESH MATERIALIZED VIEW CONCURRENTLY mv_json;

DELETE FROM t_json WHERE id = 'user-2';

REFRESH MATERIALIZED VIEW CONCURRENTLY mv_json;
SELECT id, name, info::text, age FROM mv_json ORDER BY id;
DROP TABLE t_json CASCADE;

-- Concurrent refresh with nullable unique-index keys.  Non-NULL keys use the
-- fast IN path; NULL keys need a separate NULL-safe delete.
DROP MATERIALIZED VIEW IF EXISTS mv_nullkey;
DROP TABLE IF EXISTS t_nullkey;

CREATE TABLE t_nullkey (id int, val text);
INSERT INTO t_nullkey VALUES (1, 'a'), (NULL, 'b'), (2, 'c');
CREATE MATERIALIZED VIEW mv_nullkey AS SELECT * FROM t_nullkey WITH DATA;
CREATE UNIQUE INDEX uidx_mv_nullkey_id ON mv_nullkey (id);

UPDATE t_nullkey SET val = 'b2' WHERE id IS NULL;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_nullkey;
SELECT id, val FROM mv_nullkey ORDER BY id NULLS LAST, val;

DELETE FROM t_nullkey WHERE id IS NULL;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_nullkey;
SELECT id, val FROM mv_nullkey ORDER BY id NULLS LAST, val;
DROP TABLE t_nullkey CASCADE;

-- Several rows with NULL in *every* unique-index key column but not null
-- values in the remaining.
DROP MATERIALIZED VIEW IF EXISTS mv_nullkeys_dup;
DROP TABLE IF EXISTS t_nullkeys_dup;

CREATE TABLE t_nullkeys_dup (col1 int, col2 int, col3 int, col4 int);
INSERT INTO t_nullkeys_dup VALUES (NULL, NULL, 1, 2), (NULL, NULL, 1, 3);
CREATE MATERIALIZED VIEW mv_nullkeys_dup AS SELECT * FROM t_nullkeys_dup WITH DATA;
CREATE UNIQUE INDEX uidx_mv_nullkeys_dup ON mv_nullkeys_dup (col1, col2);
SELECT col1, col2, col3, col4 FROM mv_nullkeys_dup
  ORDER BY col1 NULLS FIRST, col2 NULLS FIRST, col3, col4;

DELETE FROM t_nullkeys_dup WHERE col4 = 2;
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_nullkeys_dup;
SELECT col1, col2, col3, col4 FROM mv_nullkeys_dup
  ORDER BY col1 NULLS FIRST, col2 NULLS FIRST, col3, col4;
DROP TABLE t_nullkeys_dup CASCADE;

-- DDL run through SPI after REFRESH MATERIALIZED VIEW CONCURRENTLY in the same
-- transaction must still bump the catalog version, while the refresh itself must
-- not.
CREATE TABLE cv_base (k int PRIMARY KEY, v int);
INSERT INTO cv_base VALUES (1, 1);
CREATE MATERIALIZED VIEW cv_mv AS SELECT * FROM cv_base;
CREATE UNIQUE INDEX ON cv_mv (k);
CREATE TABLE cv_t (k int PRIMARY KEY, v int);
CREATE PROCEDURE cv_refresh_then_index() LANGUAGE plpgsql AS $$
BEGIN
  REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
END $$;
CREATE FUNCTION cv_create_index() RETURNS int LANGUAGE plpgsql AS $$
BEGIN
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
  RETURN 1;
END $$;
CREATE PROCEDURE cv_refresh_twice() LANGUAGE plpgsql AS $$
BEGIN
  REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
  REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
END $$;
SELECT oid AS cv_db_oid FROM pg_database WHERE datname = current_database() \gset
\set cv_save 'SELECT current_version AS cv_version FROM pg_yb_catalog_version WHERE db_oid = :cv_db_oid \\gset'
\set cv_bumped 'SELECT current_version > :cv_version AS bumped FROM pg_yb_catalog_version WHERE db_oid = :cv_db_oid;'

:cv_save
CALL cv_refresh_then_index();
:cv_bumped
DROP INDEX cv_t_v;

:cv_save
DO $$
BEGIN
  REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
END $$;
:cv_bumped
DROP INDEX cv_t_v;

:cv_save
BEGIN;
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
SELECT cv_create_index();
COMMIT;
:cv_bumped
DROP INDEX cv_t_v;

-- The refresh is rolled back by the exception block before the DDL runs.
:cv_save
DO $$
BEGIN
  BEGIN
    REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
    PERFORM 1 / 0;
  EXCEPTION WHEN division_by_zero THEN NULL;
  END;
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
END $$;
:cv_bumped

-- The refresh fails on duplicate rows and the exception block catches it.
CREATE TABLE cv_dup_base (k int, v int);
CREATE MATERIALIZED VIEW cv_dup_mv AS SELECT * FROM cv_dup_base;
CREATE UNIQUE INDEX ON cv_dup_mv (k);
INSERT INTO cv_dup_base VALUES (1, 1), (1, 1);
DROP INDEX cv_t_v;
:cv_save
DO $$
BEGIN
  BEGIN
    REFRESH MATERIALIZED VIEW CONCURRENTLY cv_dup_mv;
  EXCEPTION WHEN cardinality_violation THEN NULL;
  END;
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
END $$;
:cv_bumped

-- REFRESH MATERIALIZED VIEW CONCURRENTLY alone does not bump the catalog version.
:cv_save
CALL cv_refresh_twice();
DO $$ BEGIN REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv; END $$;
BEGIN;
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
COMMIT;
:cv_bumped

-- DDL run by an event trigger that the refresh fires is not part of the refresh.
CREATE FUNCTION cv_evt_create_index() RETURNS event_trigger LANGUAGE plpgsql AS $$
BEGIN
  CREATE INDEX NONCONCURRENTLY cv_t_v ON cv_t (v);
END $$;
CREATE EVENT TRIGGER cv_evt ON ddl_command_end
  WHEN TAG IN ('REFRESH MATERIALIZED VIEW') EXECUTE FUNCTION cv_evt_create_index();
DROP INDEX cv_t_v;
:cv_save
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
:cv_bumped
DROP EVENT TRIGGER cv_evt;
-- DDL run by an event trigger that the refresh's own temp-table DDL fires is not
-- part of the refresh either.
CREATE EVENT TRIGGER cv_evt_create_table ON ddl_command_end
  WHEN TAG IN ('CREATE TABLE') EXECUTE FUNCTION cv_evt_create_index();
DROP INDEX cv_t_v;
:cv_save
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
:cv_bumped
DROP EVENT TRIGGER cv_evt_create_table;
-- Rolling back a refresh must not leave the session in the refresh's
-- security-restricted context.
BEGIN;
REFRESH MATERIALIZED VIEW CONCURRENTLY cv_mv;
ROLLBACK;
CREATE TEMP TABLE cv_tmp (a int);
DROP TABLE cv_tmp;
DROP TABLE cv_t;
DROP TABLE cv_base CASCADE;
DROP TABLE cv_dup_base CASCADE;
DROP PROCEDURE cv_refresh_then_index, cv_refresh_twice;
DROP FUNCTION cv_create_index, cv_evt_create_index;

-- ANALYZE of only temporary tables does not bump the catalog version, at top
-- level or through SPI. An ANALYZE that writes nothing skips the bump anyway,
-- so each one below runs on changed data.
CREATE TABLE cv_perm (k int PRIMARY KEY);
CREATE TEMP TABLE cv_tmp1 (a int);
CREATE TEMP TABLE cv_tmp2 (a int);
INSERT INTO cv_tmp1 VALUES (1);
:cv_save
ANALYZE cv_tmp1;
INSERT INTO cv_tmp1 VALUES (2);
INSERT INTO cv_tmp2 VALUES (1);
ANALYZE cv_tmp1, cv_tmp2;
INSERT INTO cv_tmp1 VALUES (3);
DO $$ BEGIN ANALYZE cv_tmp1; END $$;
:cv_bumped
-- ANALYZE of a permanent table bumps it, including from a DO block or a
-- procedure.
INSERT INTO cv_perm VALUES (1);
:cv_save
DO $$ BEGIN ANALYZE cv_perm; END $$;
:cv_bumped
CREATE PROCEDURE cv_analyze() LANGUAGE plpgsql AS $$
BEGIN
  ANALYZE cv_perm;
END $$;
INSERT INTO cv_perm VALUES (2), (3);
:cv_save
CALL cv_analyze();
:cv_bumped
INSERT INTO cv_tmp1 VALUES (4);
INSERT INTO cv_perm SELECT generate_series(10, 20);
:cv_save
ANALYZE cv_tmp1, cv_perm;
:cv_bumped
DROP PROCEDURE cv_analyze;
DROP TABLE cv_tmp1, cv_tmp2;
-- Rolling back an ANALYZE must not leak a SET ROLE that was rolled back with it.
CREATE ROLE cv_role;
GRANT SELECT ON cv_perm TO cv_role;
BEGIN;
SET ROLE cv_role;
ANALYZE cv_perm;
ROLLBACK;
SELECT current_user = session_user AS user_restored, current_setting('role') AS role;
DROP OWNED BY cv_role;
DROP ROLE cv_role;
DROP TABLE cv_perm;
