-- Testing IN queries on hash keys
set yb_enable_hash_batch_in = false;
CREATE TABLE test_method (h1 int, a int, b int, c int, d int, e int, PRIMARY KEY (h1 HASH));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (2, 1, 1, 2, 2, 12), (19, 1, 2, 1, 3, 13), (3, 1, 2, 2, 4, 14), (4, 2, 1, 1, 5, 15), (5, 2, 1, 2, 6, 16), (6, 2, 2, 1, 7, 17), (7, 2, 2, 2, 8, 18), (8, 2, 999, 2, 8, 18), (9, 0, 1, 1, 9, 19), (10, 1, 1, 2, 10, 20), (11, 3, 1, 1, 1, 11), (12, 3, 1, 2, 2, 12), (13, 3, 2, 1, 3, 13), (14, 3, 2, 2, 4, 14), (15, 4, 1, 1, 1, 11), (16, 4, 1, 2, 2, 12), (17, 4, 2, 1, 3, 13), (18, 4, 2, 2, 4, 14);
SELECT * FROM test_method where h1 = 1;
SELECT * FROM test_method where h1 = 1 AND a = 1;
SELECT * FROM test_method where h1 = 1 AND b = 2;
SELECT * FROM test_method where h1 = 2 AND a = 1 AND b = 1;
SELECT * FROM test_method where h1 = 1 AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND a = 1;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND a = 1;

set yb_enable_hash_batch_in = true;
SELECT * FROM test_method where h1 = 1;
SELECT * FROM test_method where h1 = 1 AND a = 1;
SELECT * FROM test_method where h1 = 1 AND b = 2;
SELECT * FROM test_method where h1 = 2 AND a = 1 AND b = 1;
SELECT * FROM test_method where h1 = 1 AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND a = 1;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND a = 1;
DROP TABLE test_method;

-- Testing IN queries on range keys
set yb_enable_hash_batch_in = false;
CREATE TABLE test_method (r1 int, a int, b int, c int, d int, e int, PRIMARY KEY (r1 ASC));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (2, 1, 1, 2, 2, 12), (19, 1, 2, 1, 3, 13), (3, 1, 2, 2, 4, 14), (4, 2, 1, 1, 5, 15), (5, 2, 1, 2, 6, 16), (6, 2, 2, 1, 7, 17), (7, 2, 2, 2, 8, 18), (8, 2, 999, 2, 8, 18), (9, 0, 1, 1, 9, 19), (10, 1, 1, 2, 10, 20), (11, 3, 1, 1, 1, 11), (12, 3, 1, 2, 2, 12), (13, 3, 2, 1, 3, 13), (14, 3, 2, 2, 4, 14), (15, 4, 1, 1, 1, 11), (16, 4, 1, 2, 2, 12), (17, 4, 2, 1, 3, 13), (18, 4, 2, 2, 4, 14);
SELECT * FROM test_method where r1 = 1;
SELECT * FROM test_method where r1 = 1 AND a = 1;
SELECT * FROM test_method where r1 = 1 AND b = 2;
SELECT * FROM test_method where r1 = 2 AND a = 1 AND b = 1;
SELECT * FROM test_method where r1 = 1 AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND b = 2;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a = 1 AND b = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
set yb_enable_hash_batch_in = true;
SELECT * FROM test_method where r1 = 1;
SELECT * FROM test_method where r1 = 1 AND a = 1;
SELECT * FROM test_method where r1 = 1 AND b = 2;
SELECT * FROM test_method where r1 = 2 AND a = 1 AND b = 1;
SELECT * FROM test_method where r1 = 1 AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND b = 2;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a = 1 AND b = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
DROP TABLE test_method;

-- Testing IN queries on multi column hash keys
CREATE TABLE test_method (h1 int, h2 int, a int, b int, v1 int, v2 int, PRIMARY KEY ((h1, h2) HASH));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (2, 1, 1, 2, 2, 12), (19, 1, 2, 1, 3, 13), (3, 1, 2, 2, 4, 14), (4, 2, 1, 1, 5, 15), (5, 2, 1, 2, 6, 16), (6, 2, 2, 1, 7, 17), (7, 2, 2, 2, 8, 18), (8, 2, 999, 2, 8, 18);
INSERT INTO test_method VALUES (9, 0, 1, 1, 9, 19), (10, 1, 1, 2, 10, 20), (11, 3, 1, 1, 1, 11), (12, 3, 1, 2, 2, 12),   (13, 3, 2, 1, 3, 13), (14, 3, 2, 2, 4, 14), (15, 4, 1, 1, 1, 11), (16, 4, 1, 2, 2, 12), (17, 4, 2, 1, 3, 13), (18, 4, 2, 2, 4, 14);
set yb_enable_hash_batch_in = false;
SELECT * FROM test_method where h1 = 1 ;
SELECT * FROM test_method where h1 = 1 AND h2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND a = 1;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND a = 1 AND b = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);

set yb_enable_hash_batch_in = true;
SELECT * FROM test_method where h1 = 1 ;
SELECT * FROM test_method where h1 = 1 AND h2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND a = 1;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND a = 1 AND b = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
DROP TABLE test_method;

-- Testing IN queries on multi column range keys
CREATE TABLE test_method (r1 int, r2 int, a int, b int, v1 int, v2 int, PRIMARY KEY (r1 ASC, r2 ASC));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (2, 1, 1, 2, 2, 12), (19, 1, 2, 1, 3, 13), (3, 1, 2, 2, 4, 14), (4, 2, 1, 1, 5, 15), (5, 2, 1, 2, 6, 16), (6, 2, 2, 1, 7, 17), (7, 2, 2, 2, 8, 18), (8, 2, 999, 2, 8, 18);
INSERT INTO test_method VALUES (9, 0, 1, 1, 9, 19), (10, 1, 1, 2, 10, 20), (11, 3, 1, 1, 1, 11), (12, 3, 1, 2, 2, 12),   (13, 3, 2, 1, 3, 13), (14, 3, 2, 2, 4, 14), (15, 4, 1, 1, 1, 11), (16, 4, 1, 2, 2, 12), (17, 4, 2, 1, 3, 13), (18, 4, 2, 2, 4, 14);
set yb_enable_hash_batch_in = false;
SELECT * FROM test_method where r1 = 1 ;
SELECT * FROM test_method where r1 = 1 AND r2 = 2;
SELECT * FROM test_method where r1 = 1 AND r2 = 2 AND a = 1;
SELECT * FROM test_method where r1 = 1 AND r2 = 2 AND a = 1 AND b = 2;
SELECT * FROM test_method where r1 = 1 AND r2 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a = 1 AND b = 2;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
set yb_enable_hash_batch_in = true;
SELECT * FROM test_method where r1 = 1 ;
SELECT * FROM test_method where r1 = 1 AND r2 = 2;
SELECT * FROM test_method where r1 = 1 AND r2 = 2 AND a = 1;
SELECT * FROM test_method where r1 = 1 AND r2 = 2 AND a = 1 AND b = 2;
SELECT * FROM test_method where r1 = 1 AND r2 = 1 AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2);
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a = 1;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a = 1 AND b = 2;
SELECT * FROM test_method where r1 IN (1, 2, 3, 4) AND r2 IN (1, 2) AND a IN (1, 2, 3, 4) AND b IN (1, 2, 3, 4);
DROP TABLE test_method;


-- Testing IN queries on multi column hash and range keys
CREATE TABLE test_method (h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, PRIMARY KEY ((h1, h2) HASH, r1, r2));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (1, 1, 1, 2, 2, 12), (1, 1, 2, 1, 3, 13), (1, 1, 2, 2, 4, 14), (1, 2, 1, 1, 5, 15), (1, 2, 1, 2, 6, 16), (1, 2, 2, 1, 7, 17), (1, 2, 2, 2, 8, 18), (1, 2, 999, 2, 8, 18);
INSERT INTO test_method VALUES (2, 0, 1, 1, 9, 19), (2, 1, 1, 2, 10, 20), (1, 3, 1, 1, 1, 11), (1, 3, 1, 2, 2, 12), (1, 3, 2, 1, 3, 13), (1, 3, 2, 2, 4, 14), (1, 4, 1, 1, 1, 11), (1, 4, 1, 2, 2, 12), (1, 4, 2, 1, 3, 13), (1, 4, 2, 2, 4, 14);

set yb_enable_hash_batch_in = false;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 ;
SELECT * FROM test_method where h1 = 1 ;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1 AND r2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1 AND r2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1 AND r2 = 2;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1 AND r2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);

set yb_enable_hash_batch_in = true;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 ;
SELECT * FROM test_method where h1 = 1 ;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1 AND r2 = 2;
SELECT * FROM test_method where h1 = 1 AND h2 = 2 AND r1 = 1 AND r2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 = 1 AND h2 = 1 AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1 AND r2 = 2;
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 = 1 AND r2 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
SELECT * FROM test_method where h1 IN (1, 2, 3, 4) AND h2 IN (1, 2) AND r1 IN (1, 2, 3, 4) AND r2 IN (1, 2, 3, 4);
DROP TABLE test_method;

CREATE TABLE test_method (h1 int, h2 int, h3 int, h4 int, v1 int, v2 int, PRIMARY KEY ((h1, h2, h3, h4) HASH));
INSERT INTO test_method VALUES (1, 1, 1, 1, 1, 11), (1, 1, 1, 2, 2, 12), (1, 1, 2, 1, 3, 13), (1, 1, 2, 2, 4, 14), (1, 2, 1, 1, 5, 15), (1, 2, 1, 2, 6, 16), (1, 2, 2, 1, 7, 17), (1, 2, 2, 2, 8, 18), (1, 2, 999, 2, 8, 18);
INSERT INTO test_method VALUES (2, 0, 1, 1, 9, 19), (2, 1, 1, 2, 10, 20), (1, 3, 1, 1, 1, 11), (1, 3, 1, 2, 2, 12), (1, 3, 2, 1, 3, 13), (1, 3, 2, 2, 4, 14), (1, 4, 1, 1, 1, 11), (1, 4, 1, 2, 2, 12), (1, 4, 2, 1, 3, 13), (1, 4, 2, 2, 4, 14);
-- Set a high work_mem limit so we don't limit the size of each request batch
SET work_mem = 2147483647;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method WHERE h1 IN (1,2,3,4,5,6,7,8,9,0) AND h2 IN (1,2,3,4,5,6,7,8,9,0) AND h3 IN (1,2,3,4,5,6,7,8,9,0) AND h4 IN (1,2,3,4,5,6,7,8,9,0);

-- Set a low work_mem limit so we limit the size of each request batch and cause
-- more requests to be sent out.
SET work_mem = 64;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) SELECT * FROM test_method WHERE h1 IN (1,2,3,4,5,6,7,8,9,0) AND h2 IN (1,2,3,4,5,6,7,8,9,0) AND h3 IN (1,2,3,4,5,6,7,8,9,0) AND h4 IN (1,2,3,4,5,6,7,8,9,0);
DROP TABLE test_method;

CREATE TABLE sample (h int primary key);
INSERT INTO sample VALUES (24), (262095);
SELECT * FROM sample WHERE h in (24, 262095) ORDER BY h;
DROP TABLE sample;

CREATE TABLE in_with_single_asc_key (r INT, v INT, PRIMARY KEY(r ASC)) SPLIT AT VALUES((100));
INSERT INTO in_with_single_asc_key VALUES(1, 1), (3, 3), (2, 2), (101, 101), (102, 102), (103, 103);
SELECT * FROM in_with_single_asc_key WHERE r IN (1, 3, 2, 102, 103, 101) ORDER BY r ASC;
SELECT * FROM in_with_single_asc_key WHERE r IN (1, 3, 2, 102, 103, 101) ORDER BY r DESC;
DROP TABLE in_with_single_asc_key;

CREATE TABLE in_with_single_desc_key (r INT, v INT, PRIMARY KEY(r DESC)) SPLIT AT VALUES((100));
INSERT INTO in_with_single_desc_key VALUES(1, 1), (3, 3), (2, 2), (101, 101), (102, 102), (103, 103);
SELECT * FROM in_with_single_desc_key WHERE r IN (1, 3, 2, 102, 103, 101) ORDER BY r ASC;
SELECT * FROM in_with_single_desc_key WHERE r IN (1, 3, 2, 102, 103, 101) ORDER BY r DESC;
DROP TABLE in_with_single_desc_key;

CREATE TABLE in_with_compound_asc_key (r1 INT, r2 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC)) SPLIT AT VALUES((1, 100));
INSERT INTO in_with_compound_asc_key VALUES(1, 1, 1), (1, 3, 3), (1, 2, 2), (1, 101, 101), (1, 102, 102), (1, 103, 103);
SELECT * FROM in_with_compound_asc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 DESC, r2 DESC;
SELECT * FROM in_with_compound_asc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 DESC, r2 ASC;
SELECT * FROM in_with_compound_asc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 ASC, r2 DESC;
SELECT * FROM in_with_compound_asc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 ASC, r2 ASC;
DROP TABLE in_with_compound_asc_key;

CREATE TABLE in_with_compound_desc_key (r1 INT, r2 INT, v INT, PRIMARY KEY(r1 ASC, r2 DESC)) SPLIT AT VALUES((1, 100));
INSERT INTO in_with_compound_desc_key VALUES(1, 1, 1), (1, 3, 3), (1, 2, 2), (1, 101, 101), (1, 102, 102), (1, 103, 103);
SELECT * FROM in_with_compound_desc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 DESC, r2 DESC;
SELECT * FROM in_with_compound_desc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 DESC, r2 ASC;
SELECT * FROM in_with_compound_desc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 ASC, r2 DESC;
SELECT * FROM in_with_compound_desc_key WHERE r1 = 1 AND r2 IN (1, 3, 2, 102, 103, 101) ORDER BY r1 ASC, r2 ASC;
DROP TABLE in_with_compound_desc_key;
