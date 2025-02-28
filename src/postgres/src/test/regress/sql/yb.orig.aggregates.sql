-- YB AGGREGATES TEST (for pushdown)

SET yb_enable_bitmapscan = true;

--
-- Test basic aggregates and verify overflow is handled properly.
--

-- avoid bit-exact output here because operations may not be bit-exact.
SET extra_float_digits = 0;

CREATE TABLE ybaggtest (
    id         int PRIMARY KEY,
    int_2      int2,
    int_4      int4,
    int_8      int8,
    float_4    float4,
    float_8    float8
);
CREATE INDEX NONCONCURRENTLY ybaggtestindex ON ybaggtest (
    (int_8, int_2) HASH,
    float_4 DESC,
    int_4 ASC
) INCLUDE (float_8);
CREATE INDEX NONCONCURRENTLY ybaggtestindexsimple ON ybaggtest (
    int_4 ASC
);

-- Insert maximum integer values multiple times to force overflow on SUM (both in DocDB and PG).
INSERT INTO ybaggtest VALUES (1, 32767, 2147483647, 9223372036854775807, 1.1, 2.2);
INSERT INTO ybaggtest
    SELECT series, t.int_2, t.int_4, t.int_8, t.float_4, t.float_8
    FROM ybaggtest as t CROSS JOIN generate_series(2, 100) as series;

-- Verify COUNT(...) returns proper value.
\set explain 'EXPLAIN (COSTS OFF)'
\set ss '/*+SeqScan(ybaggtest)*/'
\set ios '/*+IndexOnlyScan(ybaggtest ybaggtestindex)*/'
\set is '/*+IndexScan(ybaggtest ybaggtestindexsimple)*/'
\set bs '/*+BitmapScan(ybaggtest)*/'
\set query 'SELECT COUNT(*) FROM ybaggtest'
\set isquery ':query WHERE int_4 > 0'
\set run ':explain :query; :explain :ss :query; :explain :ios :query; :explain :is :isquery; :explain :bs :isquery; :query; :ss :query; :ios :query; :is :isquery; :bs :isquery'
:run;
\set query 'SELECT COUNT(0) FROM ybaggtest'
:run;
\set query 'SELECT COUNT(NULL) FROM ybaggtest'
:run;

-- Delete row, verify COUNT(...) returns proper value.
DELETE FROM ybaggtest WHERE id = 100;
SELECT COUNT(*) FROM ybaggtest;
/*+IndexOnlyScan(ybaggtest ybaggtestindex)*/
SELECT COUNT(*) FROM ybaggtest;
SELECT COUNT(0) FROM ybaggtest;
/*+IndexOnlyScan(ybaggtest ybaggtestindex)*/
SELECT COUNT(0) FROM ybaggtest;

-- Verify selecting different aggs for same column works.
\set query 'SELECT SUM(int_4), MAX(int_4), MIN(int_4), SUM(int_2), MAX(int_2), MIN(int_2) FROM ybaggtest'
:run;

-- Verify SUMs are correct for all fields and do not overflow.
\set query 'SELECT SUM(int_2), SUM(int_4), SUM(int_8), SUM(float_4), SUM(float_8) FROM ybaggtest'
:run;
-- ...and do the same query excluding the int_8 column to test agg pushdown.
-- TODO(#16289): remove this.
\set query 'SELECT SUM(int_2), SUM(int_4), SUM(float_4), SUM(float_8) FROM ybaggtest'
:run;

-- Verify shared aggregates work as expected.
\set query 'SELECT SUM(int_4), SUM(int_4) + 1 FROM ybaggtest'
:run;

-- Verify NaN float values are respected by aggregates.
INSERT INTO ybaggtest (id, int_4, float_4, float_8) VALUES (101, 1, 'NaN', 'NaN');
\set query 'SELECT COUNT(float_4), SUM(float_4), MAX(float_4), MIN(float_4) FROM ybaggtest'
:run;
\set query 'SELECT COUNT(float_8), SUM(float_8), MAX(float_8), MIN(float_8) FROM ybaggtest'
:run;

-- In case indexquals are planned to be rechecked, pushdown should be avoided.
-- These queries skip Index Scan because the Index Scan hint forces using ybaggtestindexsimple,
-- which only contains int_4.
\set run_ss_ios_bs ':explain :query; :explain :ss :query; :explain :ios :query; :explain :bs :query; :query; :ss :query; :ios :query; :bs :query'
\set query 'SELECT COUNT(*) FROM ybaggtest WHERE float_4 > 0'
:run_ss_ios_bs;
\set query 'SELECT COUNT(*) FROM ybaggtest WHERE int_4 > 0 AND float_8 > 0'
:run_ss_ios_bs;
\set query 'SELECT COUNT(*) FROM ybaggtest WHERE int_8 = 9223372036854775807'
:run_ss_ios_bs;
\set query 'SELECT COUNT(*) FROM ybaggtest WHERE int_8 = 9223372036854775807 AND int_2 = 32767'
:run_ss_ios_bs;

-- In case preliminary check might happen, pushdown should be avoided.
\set query 'SELECT MAX(a.int_4) FROM ybaggtest AS a LEFT JOIN ybaggtest AS b ON a.id = b.id WHERE a.int_4 = 1 AND a.int_4 BETWEEN 7 AND 14'
:explain :query; :query;

-- Negative tests - pushdown not supported
EXPLAIN (COSTS OFF) SELECT int_2, COUNT(*), SUM(int_4) FROM ybaggtest GROUP BY int_2;
EXPLAIN (COSTS OFF) SELECT DISTINCT int_8 FROM ybaggtest;
EXPLAIN (COSTS OFF) SELECT COUNT(distinct int_4), SUM(int_4) FROM ybaggtest;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM pg_type WHERE oid = 21 AND oid < 0;

--
-- Test NULL rows are handled properly by COUNT.
--
-- Create table without primary key.
CREATE TABLE ybaggtest2 (
    a int
);

-- Create index where column a is not part of the key.
CREATE INDEX NONCONCURRENTLY ybaggtest2index ON ybaggtest2 ((1)) INCLUDE (a);

-- Insert NULL rows.
INSERT INTO ybaggtest2 VALUES (NULL), (NULL);

-- Insert regular rows.
INSERT INTO ybaggtest2 VALUES (1), (2), (3);

-- Verify NULL rows are included in COUNT(*) but not in COUNT(row).
\set ss '/*+SeqScan(ybaggtest2)*/'
\set ios '/*+IndexOnlyScan(ybaggtest2 ybaggtest2index)*/'
\set query 'SELECT COUNT(*) FROM ybaggtest2'
-- tests using run_ss_ios cannot use a plain index scan (so they also cannot use a bitmap index scan)
-- adding a hint would just re-test sequential scan
\set run_ss_ios ':explain :query; :explain :ss :query; :explain :ios :query; :ss :query; :ios :query;'
:run_ss_ios;
\set query 'SELECT COUNT(a) FROM ybaggtest2'
:run_ss_ios;
\set query 'SELECT COUNT(*), COUNT(a) FROM ybaggtest2'
:run_ss_ios;

-- Verify MAX/MIN respect NULL values.
\set query 'SELECT MAX(a), MIN(a) FROM ybaggtest2'
:run_ss_ios;

-- Verify SUM/MAX/MIN work as expected with constant arguments.
\set query 'SELECT SUM(2), MAX(2), MIN(2) FROM ybaggtest2'
:run_ss_ios;
\set query 'SELECT SUM(NULL::int), MAX(NULL), MIN(NULL) FROM ybaggtest2'
:run_ss_ios;
-- Verify IS NULL, IS NOT NULL quals.
\set query 'SELECT COUNT(*) FROM ybaggtest2 WHERE a IS NULL'
:run_ss_ios;
\set query 'SELECT COUNT(*) FROM ybaggtest2 WHERE a IS NOT NULL'
:run_ss_ios;

--
-- Test column created with default value.
--
CREATE TABLE digit(k INT PRIMARY KEY, v TEXT NOT NULL);
INSERT INTO digit VALUES(1, 'one'), (2, 'two'), (3, 'three'), (4, 'four'), (5, 'five'), (6, 'six');
CREATE TABLE test(k INT PRIMARY KEY);
ALTER TABLE test ADD v1 int DEFAULT 5;
ALTER TABLE test ADD v2 int DEFAULT 10;
CREATE INDEX NONCONCURRENTLY testindex ON test (k ASC) INCLUDE (v1, v2);
INSERT INTO test VALUES(1), (2), (3);
\set ss '/*+SeqScan(test)*/'
\set ios '/*+IndexOnlyScan(test testindex)*/'
\set is '/*+IndexScan(test testindex)*/'
\set bs '/*+BitmapScan(test)*/'
\set query 'SELECT COUNT(*) FROM test'
\set isquery ':query WHERE k > 0'
:run;
\set query 'SELECT COUNT(k) FROM test'
:run;
\set query 'SELECT COUNT(v1) FROM test'
:run;
\set query 'SELECT COUNT(v2) FROM test'
:run;
\set outerquery1 'SELECT * FROM digit AS d INNER JOIN'
\set innerquery 'SELECT COUNT(v2) AS count FROM test'
\set outerquery2 'AS c ON (d.k = c.count)'
\set query ':outerquery1 (:innerquery) :outerquery2'
\set isquery ':outerquery1 (:innerquery WHERE k > 0) :outerquery2'
:run;
INSERT INTO test VALUES(4, NULL, 10), (5, 5, NULL), (6, 5, NULL);
\set query 'SELECT COUNT(*) FROM test'
\set isquery ':query WHERE k > 0'
:run;
\set query 'SELECT COUNT(k) FROM test'
:run;
\set query 'SELECT COUNT(v1) FROM test'
:run;
\set query 'SELECT COUNT(v2) FROM test'
:run;
\set innerquery 'SELECT COUNT(*) AS count FROM test'
\set query ':outerquery1 (:innerquery) :outerquery2'
\set isquery ':outerquery1 (:innerquery WHERE k > 0) :outerquery2'
:run;
\set innerquery 'SELECT COUNT(k) AS count FROM test'
:run;
\set innerquery 'SELECT COUNT(v1) AS count FROM test'
:run;
\set innerquery 'SELECT COUNT(v2) AS count FROM test'
:run;

DROP TABLE test;
DROP TABLE digit;

--
-- Test dropped column.
--
CREATE TABLE test(K INT PRIMARY KEY, v1 INT NOT NULL, v2 INT NOT NULL);
CREATE INDEX NONCONCURRENTLY testindex ON test (K ASC) INCLUDE (v2);
INSERT INTO test VALUES(1, 1, 1), (2, 2, 2), (3, 3, 3);
AlTER TABLE test DROP v1;
\set query 'SELECT MIN(v2) FROM test'
\set isquery ':query WHERE K > 0'
:run;
\set query 'SELECT MAX(v2) FROM test'
:run;
\set query 'SELECT SUM(v2) FROM test'
:run;
\set query 'SELECT COUNT(v2) FROM test'
:run;

--
-- Test https://github.com/yugabyte/yugabyte-db/issues/10085: avoid pushdown
-- for certain cases.
--
-- Original test case that had postgres FATAL:
CREATE TABLE t1(c0 DECIMAL );
CREATE INDEX NONCONCURRENTLY t1index ON t1 (c0 ASC);
INSERT INTO t1(c0) VALUES(0.4632167437031089463062016875483095645904541015625), (0.82173140818865475498711248292238451540470123291015625), (0.69990454445895500246166420765803195536136627197265625), (0.7554730989898816861938257716246880590915679931640625);
ALTER TABLE  ONLY t1 FORCE ROW LEVEL SECURITY, DISABLE ROW LEVEL SECURITY, NO FORCE ROW LEVEL SECURITY;
INSERT INTO t1(c0) VALUES(0.9946693818538820952568357824929989874362945556640625), (0.13653666831997435249235195442452095448970794677734375), (0.3359001510719556993223022800520993769168853759765625), (0.312027233370160583802999099134467542171478271484375);
\set ss '/*+SeqScan(t1)*/'
\set ios '/*+IndexOnlyScan(t1 t1index)*/'
\set is '/*+IndexScan(t1 t1index)*/'
\set bs '/*+BitmapScan(t1)*/'
\set outerquery1 'SELECT SUM(count) FROM'
\set innerquery 'SELECT (CAST(((((''[-1962327130,2000870418)''::int4range)*(''(-1293215916,183586536]''::int4range)))-(((''[-545024026,526859443]''::int4range)*(NULL)))) AS VARCHAR)~current_query())::INT as count FROM ONLY t1'
\set outerquery2 'as res'
\set query ':outerquery1 (:innerquery) :outerquery2'
\set isquery ':outerquery1 (:innerquery WHERE c0 > 0) :outerquery2'
:run;

-- Simplified test case that had postgres FATAL:
CREATE TABLE t2(c0 DECIMAL );
CREATE INDEX NONCONCURRENTLY t2index ON t2 (c0 ASC);
INSERT INTO t2 VALUES(1), (2), (3);
\set ss '/*+SeqScan(t2)*/'
\set ios '/*+IndexOnlyScan(t2 t2index)*/'
\set is '/*+IndexScan(t2 t2index)*/'
\set bs '/*+BitmapScan(t2)*/'
\set outerquery1 'SELECT SUM(r) < 6 from'
\set innerquery 'SELECT random() as r from t2'
:run;

-- Simplified test case that had postgres FATAL:
CREATE TABLE t3(c0 DECIMAL );
CREATE INDEX NONCONCURRENTLY t3index ON t3 (c0 ASC);
INSERT INTO t3 VALUES(1), (2), (3);
\set ss '/*+SeqScan(t3)*/'
\set ios '/*+IndexOnlyScan(t3 t3index)*/'
\set is '/*+IndexScan(t3 t3index)*/'
\set bs '/*+BitmapScan(t3)*/'
\set outerquery1 'SELECT SUM(r) from'
\set innerquery 'SELECT (NULL=random())::int as r from t3'
:run;

-- Test case that did not have postgres FATAL but showed wrong result 't':
CREATE TABLE t4(c0 FLOAT8);
CREATE INDEX NONCONCURRENTLY t4index ON t4 (c0 ASC);
INSERT INTO t4 VALUES(1), (2), (3);
\set ss '/*+SeqScan(t4)*/'
\set ios '/*+IndexOnlyScan(t4 t4index)*/'
\set is '/*+IndexScan(t4 t4index)*/'
\set bs '/*+BitmapScan(t4)*/'
\set outerquery1 'SELECT SUM(r) = 6 from'
\set innerquery 'SELECT random() as r from t4'
:run;

---
--- SPLIT
---
CREATE TABLE rsplit (i int, PRIMARY KEY (i ASC)) SPLIT AT VALUES ((2));
CREATE INDEX ON rsplit (i ASC) SPLIT AT VALUES ((3));
INSERT INTO rsplit VALUES (1), (2), (3);
\set ss '/*+SeqScan(rsplit)*/'
\set ios '/*+IndexOnlyScan(rsplit rsplit_i_idx)*/'
\set is '/*+IndexScan(rsplit rsplit_i_idx)*/'
\set bs '/*+BitmapScan(rsplit)*/'
\set query 'SELECT SUM(i) FROM rsplit WHERE i = 2'
\set isquery ':query'
:run;
\set query 'SELECT SUM(i) FROM rsplit WHERE i = 3'
:run;

--
-- System tables.
--
\set ss '/*+SeqScan(pg_type)*/'
\set ios '/*+IndexOnlyScan(pg_type pg_type_typname_nsp_index)*/'
\set is '/*+IndexScan(pg_type pg_type_typname_nsp_index)*/'
\set bs '/*+BitmapScan(pg_type)*/'
\set query 'SELECT MIN(typnamespace) FROM pg_type'
\set isquery ':query WHERE typname > '''''
:run;

--
-- Temp tables.
--
CREATE TEMP TABLE tmp (i int, j int);
CREATE INDEX ON tmp (j ASC);
INSERT INTO tmp SELECT g, -g FROM generate_series(1, 10) g;
\set ss '/*+SeqScan(tmp)*/'
\set ios '/*+IndexOnlyScan(tmp tmp_j_idx)*/'
\set is '/*+IndexScan(tmp tmp_j_idx)*/'
\set query 'SELECT SUM(j), AVG(j), COUNT(*), MAX(j) FROM tmp'
\set isquery ':query WHERE j < 0'
-- TODO(#18619): change the following to :run;
:run_ss_ios;

--
-- Prepared statements.
--
CREATE TABLE ptest(k int, i int, v int, primary key(k asc));
CREATE INDEX NONCONCURRENTLY ptestindex on ptest(i asc);
INSERT INTO ptest select i, i + 1, i + 2 from generate_series(1, 10) i;
PREPARE iss as select sum(k) from ptest where k = ANY($1);
PREPARE ioss as select sum(i) from ptest where i = ANY($1);
\set prm 'ARRAY[1, 2]'
\set issexec 'EXECUTE iss(:prm)'
\set iossexec 'EXECUTE ioss(:prm)'
\set exec ':explain :issexec; :explain :iossexec; :issexec; :iossexec;'
:exec
-- repeat multiple times to make sure the statements are not replanned and cached plan is used
\set prm 'ARRAY[3, 4]'
:exec
\set prm 'ARRAY[5, 6]'
:exec
\set prm 'ARRAY[7, 8]'
:exec
\set prm 'ARRAY[9, 10]'
:exec
\set prm 'ARRAY[11, 12]'
:exec
DEALLOCATE iss;
DEALLOCATE ioss;

-- Internal parameters test
CREATE TABLE nlptest (v1 int, v2 int);
INSERT INTO nlptest SELECT i*2-1, i*2 FROM generate_series(1,6) i;
/*+ IndexScan(t2) NestLoop(t1 t2) Leading((t1 t2)) */
SELECT * FROM (SELECT v1, v2 FROM nlptest t1) AS vw1,
    LATERAL(SELECT sum(k) FROM ptest t2 WHERE k = ANY(ARRAY[vw1.v1, vw1.v2])) AS vw2
ORDER BY v1;
/*+ IndexScan(t2) NestLoop(t1 t2) Leading((t1 t2)) */
SELECT * FROM (SELECT v1, v2 FROM nlptest t1) AS vw1,
    LATERAL(SELECT sum(i) FROM ptest t2 WHERE i = ANY(ARRAY[vw1.v1, vw1.v2])) AS vw2
ORDER BY v1;

DROP TABLE ptest;
DROP TABLE nlptest;

--
-- Colocation.
--
CREATE DATABASE co COLOCATION TRUE;
\c co
SET yb_enable_bitmapscan = true;
CREATE TABLE t (i int, j int, k int);
CREATE INDEX NONCONCURRENTLY i ON t (j, k DESC, i);
INSERT INTO t VALUES (1, 2, 3), (4, 2, 6);
\set ss '/*+SeqScan(t)*/'
\set ios '/*+IndexOnlyScan(t i)*/'
\set is '/*+IndexScan(t i)*/'
\set bs '/*+BitmapScan(t)*/'
\set query 'SELECT MAX(k), AVG(i), COUNT(*), SUM(j) FROM t'
\set isquery ':query WHERE j = 2'
:run;
