-- Test primary key ordering
CREATE TABLE pk_asc(k int, v int, PRIMARY KEY(k ASC));
INSERT INTO pk_asc VALUES (20, 2),(30, 3),(10, 1);
SELECT * FROM pk_asc;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k;
SELECT * FROM pk_asc ORDER BY k;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k DESC;
SELECT * FROM pk_asc ORDER BY k DESC;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k NULLS FIRST;
SELECT * FROM pk_asc ORDER BY k NULLS FIRST;

CREATE TABLE pk_desc(k int, v int, PRIMARY KEY(k DESC NULLS LAST));
INSERT INTO pk_desc VALUES (20, 12),(30, 13),(10, 11);
SELECT * FROM pk_desc;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k;
SELECT * FROM pk_desc ORDER BY k;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k DESC;
SELECT * FROM pk_desc ORDER BY k DESC;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k NULLS FIRST;
SELECT * FROM pk_desc ORDER BY k NULLS FIRST;

-- Testing yb_pushdown_strict_inequality
SELECT k FROM pk_desc WHERE k < 30 AND k > 10;
/*+Set(yb_pushdown_strict_inequality false)*/ SELECT k FROM pk_desc WHERE k < 30 AND k > 10;

CREATE TABLE  pk_multi(h int, r int, v text, PRIMARY KEY(h, r DESC));
INSERT INTO pk_multi(h, r, v) VALUES (1, 0, '1-0'),(1, 1, '1-1'),(1, 2, '1-2'),(1, 3, '1-3');
EXPLAIN (COSTS OFF) SELECT * FROM pk_multi WHERE h = 1;
SELECT * FROM pk_multi WHERE h = 1;

-- We should still get correct results even if hash key is unset
/*+IndexScan(pk_multi pk_multi_pkey)*/ EXPLAIN (COSTS OFF) SELECT * FROM pk_multi WHERE r IN (5,3,9,2);
/*+IndexScan(pk_multi pk_multi_pkey)*/ SELECT * FROM pk_multi WHERE r IN (5,3,9,2);

EXPLAIN (COSTS OFF) SELECT * FROM pk_multi WHERE yb_hash_code(h) = yb_hash_code(1);
SELECT * FROM pk_multi WHERE yb_hash_code(h) = yb_hash_code(1);

-- Test yb_pushdown_is_not_null
CREATE TABLE inn_hash(k int PRIMARY KEY, v int) SPLIT INTO 1 TABLETS;
CREATE INDEX ON inn_hash(v ASC);
INSERT INTO inn_hash VALUES (1,NULL),(2,102),(3,NULL),(4,104),(5,105),(6,NULL);
SELECT * FROM inn_hash WHERE v IS NOT NULL;
/*+Set(yb_pushdown_is_not_null false)*/ SELECT * FROM inn_hash WHERE v IS NOT NULL;

-- Test unique secondary index ordering
CREATE TABLE usc_asc(k int, v int);
CREATE UNIQUE INDEX ON usc_asc(v ASC NULLS FIRST);
INSERT INTO usc_asc VALUES (44, NULL),(22, 20),(33, 30),(11, 10),(44, NULL);
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v;
SELECT * FROM usc_asc ORDER BY v;
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v DESC NULLS LAST;
SELECT * FROM usc_asc ORDER BY v DESC NULLS LAST;
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v NULLS FIRST;
SELECT * FROM usc_asc ORDER BY v NULLS FIRST;

CREATE TABLE usc_multi_asc(k int, r int, v int) SPLIT INTO 1 TABLETS;
CREATE INDEX ON usc_multi_asc(k, r ASC NULLS FIRST);
INSERT INTO usc_multi_asc(k, r, v) VALUES (1, 10, 1),(1, NULL, 2),(1, 20, 3);
EXPLAIN (COSTS OFF) SELECT * FROM usc_multi_asc WHERE k = 1;
SELECT * FROM usc_multi_asc WHERE k = 1;

-- Test non-unique secondary index ordering
CREATE TABLE sc_desc(k int, v int);
CREATE INDEX ON sc_desc(v DESC NULLS LAST);
INSERT INTO sc_desc VALUES (4, NULL),(2, 20),(3, 30),(1, 10),(4, NULL);
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v;
SELECT * FROM sc_desc ORDER BY v;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v DESC NULLS LAST;
SELECT * FROM sc_desc ORDER BY v DESC NULLS LAST;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v NULLS FIRST;
SELECT * FROM sc_desc ORDER BY v NULLS FIRST;

CREATE TABLE sc_multi_desc(k int, r int, v int) SPLIT INTO 1 TABLETS;
CREATE INDEX ON sc_multi_desc(k, r DESC);
INSERT INTO sc_multi_desc(k, r, v) VALUES (1, 10, 10),(1, 10, 10),(1, NULL, 2),(1, 20, 3);
EXPLAIN (COSTS OFF) SELECT * FROM sc_multi_desc WHERE k = 1;
SELECT * FROM sc_multi_desc WHERE k = 1;

-- Testing for the case in issue #12481
CREATE INDEX range_ind ON sc_multi_desc(v ASC, r ASC);
EXPLAIN (COSTS OFF) SELECT v,r FROM sc_multi_desc WHERE v IN (2,4) and r is null;
SELECT v,r FROM sc_multi_desc WHERE v IN (2,4) and r is null;

-- Test NULLS last ordering.
CREATE TABLE sc_desc_nl(h int, r int, v int) SPLIT INTO 1 TABLETS;
CREATE INDEX on sc_desc_nl(h HASH, r DESC NULLS LAST);
INSERT INTO sc_desc_nl(h,r,v) values (1,1,1), (1,2,2), (1,3,3), (1,4,4), (1,5,5), (1, null, 6);
-- Rows should be ordered DESC NULLS LAST by r.
SELECT * FROM sc_desc_nl WHERE h = 1;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1);
SELECT * FROM sc_desc_nl WHERE h = 1 AND r >= 2;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r >= 2;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r < 4;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r < 4;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r > 1 AND r <= 4;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r > 1 AND r <= 4;

-- <value> >/>=/=/<=/< null is never true per SQL semantics.
SELECT * FROM sc_desc_nl WHERE h = 1 AND r = null;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r = null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r >= null;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r >= null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r > null;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r > null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r <= null;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r <= null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r < null;
SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r < null;

-- IS NULL should be pushed down and return the expected result.
SELECT * FROM sc_desc_nl WHERE h = 1 AND r IS null;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc_nl WHERE h = 1 AND r IS null;

SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r IS null;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc_nl WHERE yb_hash_code(h) = yb_hash_code(1) AND r IS null;

DROP TABLE sc_desc_nl;

--------------------------------------
-- Testing Selective Updation of Indices
--------------------------------------
-- create table with lot of columns
create table test (pk int primary key, col2 int, col3 int, col4 int, col5 int,
col6 int, col7 name, col8 int, col9 int);
insert into test values(1,1,1,1,1,1,'Aa',1,99);
insert into test values(2,2,2,2,2,2,'Bb',2,99);
insert into test values(3,3,3,3,3,3,'Cc',3,99);
insert into test values(4,4,4,4,4,4,'Dd',4,99);
insert into test values(5,5,5,5,5,5,'Ee',5,88);
insert into test values(6,6,6,6,6,6,'Ff',6,88);

-- Creating indices with included columns
create index idx_col3 on test(col3) include (col4,col5,col6);
create index idx_col5 on test(col5) include (col6,col7);
-- Ordering is disallowed for included columns
create index on test(col5) include (col6 hash, col7);

-- Performing a few updates and checking if subsequent commands exhibit expected behavior
update test set col3=11, col4=11 where pk=1;
select * from test;

-- testing partial index on where clause
create index idx_col9 on test(col9) where col9 = 88;
update test set col9=199 where pk=2;
update test set col9=199 where pk=5;
select * from test;
EXPLAIN (COSTS OFF) SELECT * from test where col9 = 88;
EXPLAIN (COSTS OFF) SELECT * from test where col9 = 99;
select * from test where col9 = 88;
select * from test where col9 = 99;

-- testing index on expressions
create index idx_col7 ON test(col7);
EXPLAIN (COSTS OFF) SELECT * from test where col7 = 'Dd';
EXPLAIN (COSTS OFF) SELECT * from test where lower(col7) = 'dd';
select * from test where col7 = 'Dd';
drop index idx_col7;
create index idx_col7 ON test(lower(col7));
update test set col7='DdD' where pk=4;
EXPLAIN (COSTS OFF) SELECT * from test where lower(col7) = lower('DdD');
select * from test;
select * from test where lower(col7) = lower('DdD');

-- testing multi-column indices
create index idx_col4_idx_col5_idx_col6 on test(col4, col5, col6);
update test set col4=112 where pk=1;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col4 = 112;
SELECT * FROM test WHERE col4 = 112;

update test set col4=222, col5=223 where pk=2;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col4 = 222 and col5 = 223;
SELECT * FROM test WHERE col4 = 222 and col5 = 223;

update test set col4=232, col5=345, col6=456 where pk=3;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col4 = 232 and col5 = 345 and col6 = 456;
SELECT * FROM test WHERE col4 = 232 and col5 = 345 and col6 = 456;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col5 = 345;
SELECT * FROM test WHERE col5 = 345;

update test set col5=444, col6=35 where pk=4;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col5 = 444 and col6 = 35;
SELECT * FROM test WHERE col5 = 444 and col6 = 35;

update test set col6=5554 where pk=5;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE col6 = 5554;
SELECT * FROM test WHERE col6 = 5554;

-- test index only scan with non-target column refs in qual (github issue #9176)
-- baseline, col5 is in target columns
EXPLAIN (COSTS OFF) SELECT col4, col5 FROM test WHERE col4 = 232 and col5 % 3 = 0;
SELECT col4, col5 FROM test WHERE col4 = 232 and col5 % 3 = 0;
-- same lines are expected without col5 in the target list
EXPLAIN (COSTS OFF) SELECT col4 FROM test WHERE col4 = 232 and col5 % 3 = 0;
SELECT col4 FROM test WHERE col4 = 232 and col5 % 3 = 0;

-- test index scans where the filter trivially rejects everything and
-- no request should be sent to DocDB
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test WHERE col3 = ANY('{}');
SELECT * FROM test WHERE col3 = ANY('{}');
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test WHERE col3 = ANY('{NULL}');
SELECT * FROM test WHERE col3 = ANY('{NULL}');
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT col3 FROM test WHERE col3 = ANY('{NULL}');
SELECT col3 FROM test WHERE col3 = ANY('{NULL}');
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM test WHERE col3 = ANY('{NULL, NULL}');
SELECT * FROM test WHERE col3 = ANY('{NULL, NULL}');

-- testing update on primary key
update test set pk=17 where pk=1;
update test set pk=25, col4=777 where pk=2;
select * from test;
EXPLAIN (COSTS OFF) SELECT * from test where pk=17;
select * from test where pk=17;
EXPLAIN (COSTS OFF) SELECT * from test where pk=25;
select * from test where pk=25;

-- test index scan where the column type does not match value type
CREATE TABLE pk_real(c0 REAL, PRIMARY KEY(c0 asc));
INSERT INTO pk_real(c0) VALUES(0.4);
EXPLAIN (COSTS OFF) SELECT ALL pk_real.c0 FROM pk_real WHERE ((0.6)>(pk_real.c0));
SELECT ALL pk_real.c0 FROM pk_real WHERE ((0.6)>(pk_real.c0));
EXPLAIN (COSTS OFF) SELECT ALL pk_real.c0 FROM pk_real WHERE pk_real.c0 = ANY(ARRAY[0.6, 0.4]);
-- 0.4::FLOAT4 is not equal to 0.4::DOUBLE PRECISION
SELECT ALL pk_real.c0 FROM pk_real WHERE pk_real.c0 = ANY(ARRAY[0.6, 0.4]);
INSERT INTO pk_real(c0) VALUES(0.5);
EXPLAIN (COSTS OFF) SELECT ALL pk_real.c0 FROM pk_real WHERE pk_real.c0 = 0.5;
-- 0.5::FLOAT4 is equal to 0.5::DOUBLE PRECISION
SELECT ALL pk_real.c0 FROM pk_real WHERE pk_real.c0 = 0.5;

CREATE TABLE pk_smallint(c0 SMALLINT, PRIMARY KEY(c0 asc));
INSERT INTO pk_smallint VALUES(123), (-123);
EXPLAIN (COSTS OFF) SELECT c0 FROM pk_smallint WHERE (65568 > c0);
SELECT c0 FROM pk_smallint WHERE (65568 > c0);
EXPLAIN (COSTS OFF) SELECT c0 FROM pk_smallint WHERE (c0 > -65539);
SELECT c0 FROM pk_smallint WHERE (c0 > -65539);
EXPLAIN (COSTS OFF) SELECT c0 FROM pk_smallint WHERE (c0 = ANY(ARRAY[-65539, 65568]));
SELECT c0 FROM pk_smallint WHERE (c0 = ANY(ARRAY[-65539, 65568]));

-- test any/some/all
create TABLE pk_int(c0 int, primary key(c0 ASC));
INSERT INTO pk_int VALUES (1), (2), (3), (4);
SELECT * FROM pk_int WHERE c0 IN (3, 4);
SELECT * FROM pk_int WHERE c0 NOT IN (3, 4);
SELECT * FROM pk_int WHERE c0 < ANY(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 <= ANY(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 = ANY(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 >= ANY(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 > ANY(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 < SOME(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 <= SOME(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 = SOME(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 >= SOME(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 > SOME(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 < ALL(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 <= ALL(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 = ALL(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 >= ALL(ARRAY[3, 4]);
SELECT * FROM pk_int WHERE c0 > ALL(ARRAY[3, 4]);

-- test row comparison expressions
CREATE TABLE pk_range_int_asc (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 asc, r2 asc, r3 asc));
INSERT INTO pk_range_int_asc SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (2,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, v, r3) <= (2,3,60,1);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, v, r3) <= (2,3,60,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) < (2,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) < (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (3,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (3,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) > (3,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) > (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (2,4,5);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (2,4,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,2,3) AND (r1, r2, r3) <= (1,3,2) AND r3 IN (3,2,6);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,2,3) AND (r1, r2, r3) <= (1,3,2) AND r3 IN (3,2,6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,1,5) AND (r1, r2, r3) <= (1,4,5) ORDER BY r1 DESC, r2 DESC, r3 DESC;
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) >= (1,1,5) AND (r1, r2, r3) <= (1,4,5) ORDER BY r1 DESC, r2 DESC, r3 DESC;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) = (1,6,5);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) = (1,6,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (1,6,5) AND (r1,r2,r3) < (1,6,5);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (1,6,5) AND (r1,r2,r3) < (1,6,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (1,1,5) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) > (1,2,3) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) >= (1,2,3);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (1,1,5) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) > (1,2,3) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) >= (1,2,3);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
SELECT * FROM pk_range_int_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
DROP TABLE pk_range_int_asc;

-- test row comparison expressions where we have differing column orderings
CREATE TABLE pk_range_asc_desc_asc (r1 BIGINT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 asc, r2 desc, r3 asc));
INSERT INTO pk_range_asc_desc_asc SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) <= (2,3,2);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) <= (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, v, r3) <= (2,3,60,1);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, v, r3) <= (2,3,60,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) > (8,7,3);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) > (8,7,3);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1) ORDER BY r1 DESC, r2 ASC, r3 DESC;
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1) ORDER BY r1 DESC, r2 ASC, r3 DESC;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
SELECT * FROM pk_range_asc_desc_asc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
DROP TABLE pk_range_asc_desc_asc;

CREATE TABLE pk_range_desc_asc_desc (r1 BIGINT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 desc, r2 asc, r3 desc));
INSERT INTO pk_range_desc_asc_desc SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) <= (2,3,2);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) <= (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, v, r3) <= (2,3,60,1);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, v, r3) <= (2,3,60,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1) ORDER BY r1 DESC, r2 ASC, r3 DESC;
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r2, r3) >= (1,7,2) AND (r1, r2, r3) <= (3,2,1) ORDER BY r1 DESC, r2 ASC, r3 DESC;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
SELECT * FROM pk_range_desc_asc_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
DROP TABLE pk_range_desc_asc_desc;

CREATE TABLE pk_range_int_desc (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 desc, r2 desc, r3 desc));
INSERT INTO pk_range_int_desc SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (2,3,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, v, r3) <= (2,3,60,1);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, v, r3) <= (2,3,60,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) < (2,3,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) < (2,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (3,3,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (3,3,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) > (3,3,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) > (3,3,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (2,4,5);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (2,4,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,2,3) AND (r1, r2, r3) <= (1,3,2) AND r3 IN (3,2,6);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,2,3) AND (r1, r2, r3) <= (1,3,2) AND r3 IN (3,2,6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (1,6,5) ORDER BY r1 ASC, r2 ASC, r3 ASC;
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) >= (1,4,5) AND (r1, r2, r3) <= (1,6,5) ORDER BY r1 ASC, r2 ASC, r3 ASC;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) = (1,6,5);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) = (1,6,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (1,6,5) AND (r1,r2,r3) < (1,6,5);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (1,6,5) AND (r1,r2,r3) < (1,6,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (1,1,5) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) > (1,2,3) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) >= (1,2,3);
SELECT * FROM pk_range_int_desc WHERE (r1, r2, r3) <= (1,1,5) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) > (1,2,3) AND (r1,r2,r3) < (1,2,4) AND (r1,r2,r3) >= (1,2,3);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
SELECT * FROM pk_range_int_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
SELECT * FROM pk_range_int_desc WHERE (r1, r3) <= (1,3) AND (r1,r2) < (1,3) AND (r1,r2) >= (1,2) AND (r1,r2,r3) = (1,2,3);
EXPLAIN (COSTS OFF) SELECT * FROM pk_range_int_desc WHERE r2 IN (1,3,5) ORDER BY r1 DESC, r2 DESC LIMIT 10;
SELECT * FROM pk_range_int_desc WHERE r2 IN (1,3,5) ORDER BY r1 DESC, r2 DESC LIMIT 10;
DROP TABLE pk_range_int_desc;

CREATE TABLE pk_range_int_text (r1 INT, r2 TEXT, r3 BIGINT, v INT, PRIMARY KEY(r1 asc, r2 asc, r3 asc));
INSERT INTO pk_range_int_text SELECT i/25, concat('abc', ((i/5) % 5)::TEXT), i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) <= (2,'ab2'::text,2);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) <= (2,'ab2'::text,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, v, r3) <= (2,'abc3'::text,60,1);
SELECT * FROM pk_range_int_text WHERE (r1, r2, v, r3) <= (2,'abc3'::text,60,1);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) < (2,'abc3'::text,2);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) < (2,'abc3'::text,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) <= (3,'abb3'::text,2);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) <= (3,'abb3'::text,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (3,'abc3'::text,2);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (3,'abc3'::text,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) > (3,'abc3'::text,2);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) > (3,'abc3'::text,2);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'abc4'::text,5) AND (r1, r2, r3) <= (2,'abc4'::text,5);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'abc4'::text,5) AND (r1, r2, r3) <= (2,'abc4'::text,5);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'abc2'::text,3) AND (r1, r2, r3) <= (1,'abc3'::text,2) AND r3 IN (3,2,6);
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'abc2'::text,3) AND (r1, r2, r3) <= (1,'abc3'::text,2) AND r3 IN (3,2,6);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'ab'::text,5) AND (r1, r2, r3) <= (1,'abcd'::text,5) ORDER BY r1 ASC, r2 ASC, r3 ASC;
SELECT * FROM pk_range_int_text WHERE (r1, r2, r3) >= (1,'ab'::text,5) AND (r1, r2, r3) <= (1,'abcd'::text,5) ORDER BY r1 ASC, r2 ASC, r3 ASC;
DROP TABLE pk_range_int_text;

CREATE TABLE null_test(a int, b int);
CREATE INDEX ON null_test(a asc, b asc);
INSERT INTO null_test VALUES (NULL, 9), (9, NULL), (9,8), (10,9);
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM null_test WHERE (a,b) >= (9, 8);
SELECT * FROM null_test WHERE (a,b) >= (9, 8);

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM null_test WHERE (a,b) <= (9, 8);
SELECT * FROM null_test WHERE (a,b) <= (9, 8);

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM null_test WHERE (a,b) <= (9, 8);
SELECT * FROM null_test WHERE (a,b) <= (9, 8);

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM null_test WHERE (a,b) >= (8, 9);
SELECT * FROM null_test WHERE (a,b) >= (8, 9);

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)
/*+ IndexOnlyScan(null_test) */
SELECT * FROM null_test WHERE (a,b) >= (8, 9);
/*+ IndexOnlyScan(null_test) */
SELECT * FROM null_test WHERE (a,b) >= (8, 9);


EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)
/*+ IndexOnlyScan(null_test) */
SELECT * FROM null_test WHERE a > 8 OR (a = 8 AND b >= NULL);

/*+ IndexOnlyScan(null_test) */
SELECT * FROM null_test WHERE a > 8 OR (a = 8 AND b >= NULL);


EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)
/*+ Set(enable_material OFF) Leading((t1 t2)) IndexOnlyScan(t1) IndexOnlyScan(t2) */
SELECT * FROM null_test t1 JOIN null_test t2 ON (t1.a, t1.b) >= (t2.a, t2.b);

/*+ Set(enable_material OFF) Leading((t1 t2)) IndexOnlyScan(t1) IndexOnlyScan(t2) */
SELECT * FROM null_test t1 JOIN null_test t2 ON (t1.a, t1.b) >= (t2.a, t2.b);


EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE)
/*+ Set(enable_material OFF) Leading((t1 t2)) IndexOnlyScan(t1) IndexOnlyScan(t2) */
SELECT * FROM null_test t1 JOIN null_test t2 ON t1.a > t2.a OR (t1.a = t2.a AND t1.b >= t2.b);

/*+ Set(enable_material OFF) Leading((t1 t2)) IndexOnlyScan(t1) IndexOnlyScan(t2) */
SELECT * FROM null_test t1 JOIN null_test t2 ON t1.a > t2.a OR (t1.a = t2.a AND t1.b >= t2.b);
DROP TABLE null_test;

-- make sure row comparisons don't operate on hash keys yet
CREATE TABLE pk_hash_range_int (h int, r1 int, r2 int, r3 int, PRIMARY KEY(h hash, r1 asc, r2 asc, r3 asc));
INSERT INTO pk_hash_range_int SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
/*+ IndexScan(pk_hash_range_int) */ EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_hash_range_int WHERE (r1, r2) <= (3, 2);
/*+ IndexScan(pk_hash_range_int) */ SELECT * FROM pk_hash_range_int WHERE (r1, r2) <= (3, 2);
/*+ IndexScan(pk_hash_range_int) */ EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_hash_range_int WHERE h = 1 AND (r1, r2) <= (3, 2) AND r1 <= 2;
/*+ IndexScan(pk_hash_range_int) */ SELECT * FROM pk_hash_range_int WHERE h = 1 AND (r1, r2) <= (3, 2) AND r1 <= 2;
/*+ IndexScan(pk_hash_range_int) */ EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT sum(r1) FROM pk_hash_range_int WHERE h = 1 AND (r1, r2) <= (3, 2) AND r1 <= 2;
/*+ IndexScan(pk_hash_range_int) */ SELECT sum(r1) FROM pk_hash_range_int WHERE h = 1 AND (r1, r2) <= (3, 2) AND r1 <= 2;
DROP TABLE pk_hash_range_int;

-- Test index SPLIT AT with INCLUDE clause
CREATE TABLE test_tbl (
  a INT,
  b INT,
  PRIMARY KEY (a ASC)
) SPLIT AT VALUES((1));
CREATE INDEX test_idx on test_tbl(
  b ASC
) INCLUDE (a) SPLIT AT VALUES ((1));
INSERT INTO test_tbl VALUES (1, 2),(2, 1),(4, 3),(5, 4);
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT a, b FROM test_tbl WHERE a = 4;
SELECT a, b FROM test_tbl WHERE a = 4;
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT a, b FROM test_tbl WHERE b = 4;
SELECT a, b FROM test_tbl WHERE b = 4;
DROP INDEX test_idx;
DROP TABLE test_tbl;

-- (#21004) The tests in this section validate that computation of range bounds
-- is skipped while prechecking an Index-only Scan's rescan condition.
DROP TABLE IF EXISTS aa;
DROP TABLE IF EXISTS bb;
CREATE TABLE aa (col_varchar_key VARCHAR(1), col_varchar_nokey VARCHAR(1));
CREATE TABLE bb (col_varchar_key VARCHAR(1), col_varchar_nokey VARCHAR(1));
INSERT INTO aa VALUES ('g', 'g');
INSERT INTO bb VALUES ('g', 'g');
CREATE INDEX bb_varchar_key ON bb (col_varchar_key ASC);
-- Correlation between two different tables
EXPLAIN (COSTS OFF) SELECT (
  SELECT
    COUNT(*)
  FROM
    bb AS subquery_t1
  WHERE
    subquery_t1.col_varchar_key <= table2.col_varchar_nokey AND
    subquery_t1.col_varchar_key <= table2.col_varchar_nokey
) AS field1
FROM
  aa AS table2;
-- Correlation of the same table
SELECT (
  SELECT
    COUNT(*)
  FROM
    bb AS subquery_t1
  WHERE
    subquery_t1.col_varchar_key >= table1.col_varchar_nokey AND
    subquery_t1.col_varchar_key > 'a'
) AS field1
FROM
  bb AS table1;

create table sample(a int, b int, primary key(a asc, b asc));
insert into sample values (1,1);
insert into sample values (1,2);
insert into sample values (2,1);

explain (costs off) select * from sample where b < 2 and b >= 2;
select * from sample where b < 2 and b >= 2;

explain (costs off) select * from sample where b >= 2 and b < 2;
select * from sample where b >= 2 and b < 2;

explain (costs off) select * from sample where b < 2 and b >= 2;
select * from sample where b < 2 and b >= 2;

explain (costs off) select * from sample where b <= 2 and b < 2 and b >= 2;
select * from sample where b <= 2 and b < 2 and b >= 2;

explain (costs off) select * from sample where b <= 2 and b >= 2;
select * from sample where b <= 2 and b >= 2;

explain (costs off) select * from sample where b <= 3 and b <= 2 and b >= 2;
select * from sample where b <= 3 and b <= 2 and b >= 2;

drop table sample;

-- GHI 21451 HASH Index on multiple expressions
drop table IF EXISTS t1;
create table t1 (c1 bigint, c2 jsonb, primary key ((c1)));
insert into t1 (c1,c2) values (1,'{"c3":1,"c4":1}');
insert into t1 (c1,c2) values (2,'{"c3":2,"c4":2}');
create index t1_idx on t1 (((c2->>'c3'), (c2->>'c4')) hash);

select * from t1;
/*+IndexScan(t1 t1_idx)*/ explain (costs off) select * from t1 where (c2->>'c3') = '4';
/*+IndexScan(t1 t1_idx)*/ select * from t1 where (c2->>'c3') = '4';
/*+IndexScan(t1 t1_idx)*/ explain (costs off) select * from t1 where (c2->>'c4') = '4';
/*+IndexScan(t1 t1_idx)*/ select * from t1 where (c2->>'c4') = '4';
/*+IndexScan(t1 t1_idx)*/ explain (costs off) select * from t1 where (c2->>'c3') = '4' and (c2->>'c4') = '4';
/*+IndexScan(t1 t1_idx)*/ select * from t1 where (c2->>'c3') = '4' and (c2->>'c4') = '4';

drop table t1;

create table sample(a int, primary key(a asc));
insert into sample values (0);
select * from sample where a = x'8000000000000000'::bigint;
drop table sample;

create table sample(a int2, primary key(a asc));
insert into sample values (0);
select * from sample where a = x'8000000000000000'::bigint;
select * from sample where a = x'80000000'::int;
drop table sample;

-- GHI 22832 possible fetch of a dropped column
create table t1(k1 int, k2 int, v0 int, v1 int, v2 int, primary key ((k1, k2) hash));
alter table t1 drop column v0;
create index on t1(k1, k2, v1);
-- prevents "all_ordinary_keys_bound"
set yb_pushdown_is_not_null to false;
-- inequality prevents PK usage, IN sets a scan key flag that makes v1 wanted for rechecks
explain (costs off) select v2 from t1 where k1 = 1 and k2 > 0 and v1 IN (1, 2);
select v2 from t1 where k1 = 1 and k2 > 0 and v1 IN (1, 2);
drop table t1;

create table t1(a uuid, b timestamptz, PRIMARY KEY(b asc, a asc));
insert into t1 values ('00000000-0000-0000-0000-000000000000', '2021-01-01 00:00:00');
insert into t1 values ('00000000-0000-0000-0000-000000000001', '2021-01-01 00:00:00');
explain (costs off) select * from t1 where (b, a) >= ('2021-01-01 00:00:00', '00000000-0000-0000-0000-000000000001');
select * from t1 where (b, a) >= ('2021-01-01 00:00:00', '00000000-0000-0000-0000-000000000001');
drop table t1;
