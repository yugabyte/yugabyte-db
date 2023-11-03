-- Tablespace.
-- Source table is in a tablespace.
CREATE TABLESPACE testtablespace WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"datacenter1", "zone":"rack1", "min_num_replicas":1}]}');
CREATE TABLE tbsptest1 (id int CHECK (id > 0) PRIMARY KEY, b text UNIQUE, c int DEFAULT 0) TABLESPACE testtablespace;
-- The tablespace is never copied over to the target table.
CREATE TABLE tbsptest2 (LIKE tbsptest1 INCLUDING ALL);
\d+ tbsptest2
-- Target table is in a tablespace.
CREATE TABLE tbsptest3 (LIKE tbsptest2 INCLUDING ALL) TABLESPACE testtablespace;
\d+ tbsptest3
DROP TABLE tbsptest3, tbsptest2, tbsptest1 CASCADE;
DROP TABLESPACE testtablespace;

-- Test variations of primary key.
CREATE TABLE testpk(h1 int, h2 text, d text, a int, value text, PRIMARY KEY ((h1, h2) HASH, d DESC, a ASC));
CREATE TABLE testlikepk(LIKE testpk INCLUDING INDEXES);
\d+ testlikepk
CREATE TABLE testlikenopk(LIKE testpk);
\d+ testlikenopk;
DROP TABLE testpk, testlikepk, testlikenopk CASCADE;

-- Test adding SPLIT AT syntax with copied PK.
CREATE TABLE testsplitat (
  a INT,
  b TEXT,
  PRIMARY KEY(a ASC, b ASC));
CREATE TABLE testlikesplitat(LIKE testsplitat INCLUDING INDEXES) SPLIT AT VALUES((-100, 'bar'), (250, 'foo'));
CREATE INDEX ON testlikesplitat(a ASC) SPLIT AT VALUES ((10), (20));
\d+ testlikesplitat
SELECT yb_get_range_split_clause('testlikesplitat'::regclass);
SELECT yb_get_range_split_clause('testlikesplitat_a_idx'::regclass);

-- Test adding SPLIT INTO syntax with copied PK.
CREATE TABLE testsplitinto(a INT, b text, PRIMARY KEY((a, b) HASH));
CREATE TABLE testlikesplitinto(LIKE testsplitinto INCLUDING INDEXES) SPLIT INTO 2 TABLETS;
CREATE INDEX ON testlikesplitinto(a) SPLIT INTO 5 TABLETS;
\d+ testlikesplitinto
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('testlikesplitinto'::regclass);
SELECT num_tablets, num_hash_key_columns
    FROM yb_table_properties('testlikesplitinto_a_idx'::regclass);

-- Split info is not copied.
CREATE TABLE neg_splitat (LIKE testlikesplitat INCLUDING ALL);
SELECT yb_get_range_split_clause('neg_splitat'::regclass);
SELECT yb_get_range_split_clause('neg_splitat_a_idx'::regclass);
CREATE TABLE neg_splitinto (LIKE testlikesplitinto INCLUDING ALL);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('neg_splitinto'::regclass);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('neg_splitinto_a_idx'::regclass);
DROP TABLE testsplitat, testlikesplitat, testsplitinto, testlikesplitinto, neg_splitat, neg_splitinto CASCADE;

-- Test variations of unique key index.
CREATE TABLE testunique(h1 int, h2 text, d text, a int, value text);
CREATE UNIQUE INDEX hashidx ON testunique using lsm (h1, h2) INCLUDE (a);
CREATE UNIQUE INDEX rangeidx ON testunique using lsm (d DESC, a) INCLUDE (h1);
CREATE UNIQUE INDEX hashrangeidx ON testunique using lsm ((h1, h2) HASH, a, d DESC) INCLUDE (value);

CREATE TABLE testlikeunique(LIKE testunique INCLUDING INDEXES);
\d+ testlikeunique
DROP TABLE testunique;

CREATE TABLE liketest1 (a text CHECK (length(a) > 2) PRIMARY KEY, b text DEFAULT 'abc');
CREATE INDEX liketest1_b ON liketest1 (b);
CREATE INDEX liketest1_ab ON liketest1 ((a || b));
COMMENT ON COLUMN liketest1.a IS 'A';
COMMENT ON COLUMN liketest1.b IS 'B';
COMMENT ON CONSTRAINT liketest1_a_check ON liketest1 IS 'a_check';
COMMENT ON INDEX liketest1_pkey IS 'index pkey';
COMMENT ON INDEX liketest1_b IS 'index b';

CREATE TABLE liketest2 (c text CHECK (length(c) > 2), d text, PRIMARY KEY(c ASC));
CREATE INDEX liketest2_d ON liketest2 (d);
COMMENT ON COLUMN liketest2.c IS 'C';
COMMENT ON COLUMN liketest2.d IS 'D';
COMMENT ON CONSTRAINT liketest2_c_check ON liketest2 IS 'c_check';
COMMENT ON INDEX liketest2_pkey IS 'index pkey';
COMMENT ON INDEX liketest2_d IS 'index c';

-- Test INCLUDING COMMENTS.
CREATE OR REPLACE FUNCTION get_comments(in text) RETURNS TABLE (object_type text, obj_name name, comments text)
AS $$
SELECT 'column' AS object_type, column_name AS obj_name, col_description(table_name::regclass::oid, ordinal_position) AS comments FROM information_schema.columns  WHERE table_name=$1
UNION
SELECT 'index' AS object_type, relname AS obj_name, obj_description(oid) AS comments FROM pg_class WHERE oid IN (SELECT indexrelid FROM pg_index WHERE indrelid=$1::regclass::oid)
UNION
SELECT 'constraint' AS object_type, conname AS obj_name, obj_description(oid) AS comments FROM pg_constraint WHERE conrelid=$1::regclass::oid
ORDER BY obj_name
$$ LANGUAGE SQL;

-- Without specifying INCLUDING COMMENTS, comments are not copied.
CREATE TABLE comments1 (LIKE liketest1 INCLUDING INDEXES, LIKE liketest2 INCLUDING CONSTRAINTS);
SELECT * FROM get_comments('comments1');
-- Comments are copied over if INCLUDING COMMENTS/INCLUDING ALL is specified.
CREATE TABLE comments2(LIKE liketest1 INCLUDING INDEXES INCLUDING COMMENTS, LIKE liketest2 INCLUDING CONSTRAINTS INCLUDING COMMENTS);
SELECT * FROM get_comments('comments2');
CREATE TABLE comments3(LIKE liketest1 INCLUDING ALL);
SELECT * FROM get_comments('comments3');
DROP TABLE comments1, comments2, comments3 CASCADE;
DROP FUNCTION get_comments;

-- Test INCLUDING STATISTICS.
CREATE STATISTICS liketest1_stat ON a,b FROM liketest1;
CREATE TABLE neg_stats_test (LIKE liketest1);
\d+ neg_stats_test;
CREATE TABLE stats_test1 (LIKE liketest1 INCLUDING STATISTICS);
\d+ stats_test1
CREATE TABLE stats_test2 (LIKE liketest1 INCLUDING ALL);
\d+ stats_test2
DROP TABLE neg_stats_test, stats_test1, stats_test2 CASCADE;
DROP STATISTICS liketest1_stat;

-- Test Tablegroup.
CREATE TABLEGROUP tgroup1;
-- Create table using LIKE clause in a tablegroup.
CREATE TABLE testtgroup1 (LIKE liketest1 INCLUDING DEFAULTS, LIKE liketest2 INCLUDING CONSTRAINTS) TABLEGROUP tgroup1;
\d+ testtgroup1
-- Fail because liketest2 has a hash-partitioned index.
CREATE TABLE testtgroup2 (LIKE liketest2 INCLUDING ALL) TABLEGROUP tgroup1;
DROP INDEX liketest2_d;
CREATE INDEX liketest2_d ON liketest2 (d ASC);
-- Passes now.
CREATE TABLE testtgroup2 (LIKE liketest2 INCLUDING ALL) TABLEGROUP tgroup1;
\d+ testtgroup2

-- Create table using LIKE clause from a table in a tablegroup. The tablegroup clause is not copied over.
CREATE TABLE neg_tgroup (LIKE testtgroup2 INCLUDING ALL);
\d+ neg_tgroup;

-- Cleanup
DROP TABLE liketest1, liketest2, testtgroup1, testtgroup2, neg_tgroup CASCADE;
DROP TABLEGROUP tgroup1 CASCADE;

-- Colocation
CREATE DATABASE colocation_test colocation = true;
\c colocation_test
CREATE TABLE colocate_source (k int, v1 text DEFAULT 'hello world', v2 int CHECK (v2 > 0), v3 float, PRIMARY KEY (k ASC));
CREATE TABLE uncolocated_target_test (LIKE colocate_source INCLUDING ALL) with (colocation = false);
\d uncolocated_target_test
CREATE TABLE colocated_target_test (LIKE colocate_source INCLUDING ALL);
\d colocated_target_test
-- cannot colocate hash-partitioned table.
CREATE TABLE hash_k (i int primary key) with (colocation = false);
CREATE TABLE hash_k_fail_test (LIKE hash_k INCLUDING ALL);
CREATE TABLE hash_k_test (LIKE hash_k INCLUDING ALL) with (colocation = false);
\d hash_k_test
\c yugabyte
DROP DATABASE colocation_test;

-- When using LIKE clause on a source table with indexes, the target table has indexes deduplicated.
CREATE TABLE test_dedupe_idx (hashkey int, asckey text, desckey text);
CREATE INDEX h1 ON test_dedupe_idx(hashkey);
CREATE INDEX h2 ON test_dedupe_idx(hashkey);
CREATE INDEX a1 ON test_dedupe_idx(asckey ASC);
CREATE INDEX a2 ON test_dedupe_idx(asckey ASC);
CREATE INDEX d1 ON test_dedupe_idx(desckey DESC);
CREATE INDEX d2 ON test_dedupe_idx(desckey DESC);
CREATE TABLE test_dedupe_idx_like (LIKE test_dedupe_idx INCLUDING ALL);
\d test_dedupe_idx_like
DROP TABLE test_dedupe_idx;

-- LIKE clause with temp tables.
-- Test using LIKE clause where the source table is a temp table.
CREATE TEMP TABLE temptest (k int PRIMARY KEY, v1 text DEFAULT 'hello world', v2 int CHECK (v2 > 0), v3 float UNIQUE);
CREATE INDEX ON temptest(v1);
CREATE TABLE liketemptest (LIKE temptest INCLUDING ALL);
\d liketemptest
DROP TABLE liketemptest;
-- Test using LIKE clause where source and target tables are temp tables.
CREATE TEMP TABLE liketemptest (LIKE temptest INCLUDING ALL);
-- \d liketemptest has unstable output due to temporary schemaname
-- such as pg_temp_1, pg_temp_2, etc. Use regexp_replace to change
-- it to pg_temp_xxx so that the result is stable.
select current_setting('data_directory') || 'describe.out' as desc_output_file
\gset
\o :desc_output_file
\d liketemptest
\o
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_\d+', 'pg_temp_xxx', 'g');

-- Test using LIKE clause where the target table is a temp table.
CREATE TEMP TABLE gin_test (a int[]);
CREATE INDEX ON gin_test USING GIN (a);
CREATE TABLE gin_like_test (LIKE gin_test INCLUDING ALL);
\d gin_like_test
CREATE INDEX ON gin_test (a);
CREATE TABLE gin_like_test_idx (LIKE gin_test INCLUDING ALL);
DROP TABLE liketemptest, temptest, gin_test, gin_like_test;

-- Source is a VIEW.
CREATE TABLE test_table(k INT PRIMARY KEY, v INT);
CREATE VIEW test_view AS SELECT k FROM test_table WHERE v = 10;
CREATE TABLE like_view (LIKE test_view INCLUDING ALL);
\d like_view
DROP VIEW test_view;
DROP TABLE test_table, like_view CASCADE;

-- Source is a FOREIGN TABLE.
CREATE EXTENSION file_fdw;
CREATE SERVER s1 FOREIGN DATA WRAPPER file_fdw;
CREATE FOREIGN TABLE test_foreign (id int CHECK (id > 0), b int DEFAULT 0) SERVER s1 OPTIONS ( filename 'foo');
CREATE TABLE test_foreign_like (LIKE test_foreign INCLUDING ALL);
\d test_foreign_like
DROP FOREIGN TABLE test_foreign;
DROP SERVER s1;
DROP EXTENSION file_fdw CASCADE;
DROP TABLE test_foreign_like;

-- Source is a COMPOSITE TYPE.
CREATE TYPE type_pair AS (f1 INT, f2 INT);
CREATE TYPE type_pair_with_int AS (f1 type_pair, f2 int);
CREATE TABLE test_like_type (LIKE type_pair_with_int);
\d test_like_type
DROP TABLE test_like_type;
DROP TYPE type_pair_with_int, type_pair CASCADE;
