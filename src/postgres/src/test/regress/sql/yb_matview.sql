--
-- YB tests for materialized views
--

CREATE TABLE test_yb (col int);
INSERT INTO test_yb VALUES (null);
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
explain select * from mvtest_tv7 where a @> '{6}';
CREATE INDEX NONCONCURRENTLY ON mvtest_tv7 using ybgin (a);
explain select * from mvtest_tv7 where a @> '{6}';
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