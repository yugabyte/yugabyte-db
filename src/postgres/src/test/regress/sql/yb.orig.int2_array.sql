create table int2_array_test (
    k int primary key,
    smallint_arr smallint[],
    int2_arr int2[]
    );

insert into int2_array_test (k, smallint_arr, int2_arr) values (
    1, '{32767,0,-32768}','{-32768,0,32767}');

select * from int2_array_test;

-- Invalid values for smallint
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, '{-32769}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, '{32768}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, NULL, '{-32769}');
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, NULL, '{32768}');

insert into int2_array_test (k, smallint_arr, int2_arr) values (
    10, '{-10000,1,2,3,10000}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (
    11, NULL, '{-10000,1,2,3,10000}');
select * from int2_array_test order by k;

drop table int2_array_test;

-- int2vector lsm key columns serve only equality: DocDB orders their keys by
-- bytes, not in array order.
CREATE TABLE int2vector_key_test (k int PRIMARY KEY, v int2vector, b int);
CREATE INDEX int2vector_key_test_v_idx ON int2vector_key_test (v ASC);
INSERT INTO int2vector_key_test VALUES
  (1, '', 1), (2, '1', 2), (3, '1 2', 3), (4, '2 1', 4), (5, '1 2 3', 5),
  (6, '0', 6), (7, '-1', 7), (8, '10', 8), (9, '256', 9), (10, NULL, 10);
SET enable_seqscan = off;
SET yb_enable_cbo = on;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < '1 2';
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v > '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v > '1 2';
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v BETWEEN '1' AND '2';
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v BETWEEN '1' AND '2';
-- Equality still uses the index.
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v = '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v = '1 2';
-- The index gives no order on v.
SET enable_sort = off;
SET enable_incremental_sort = off;
EXPLAIN (COSTS OFF) SELECT k FROM int2vector_key_test ORDER BY v;
SELECT k FROM int2vector_key_test ORDER BY v;
-- min and max are not pushed down; count is.
EXPLAIN (COSTS OFF) SELECT min(v), max(v) FROM int2vector_key_test;
SELECT min(v), max(v) FROM int2vector_key_test;
EXPLAIN (COSTS OFF) SELECT count(v) FROM int2vector_key_test;
SELECT count(v) FROM int2vector_key_test;
-- Merge join on v.
CREATE TABLE int2vector_key_test2 (k int PRIMARY KEY, v int2vector);
CREATE INDEX int2vector_key_test2_v_idx ON int2vector_key_test2 (v ASC);
INSERT INTO int2vector_key_test2 SELECT k, v FROM int2vector_key_test;
SET enable_hashjoin = off;
SET enable_nestloop = off;
SET yb_enable_batchednl = off;
EXPLAIN (COSTS OFF) SELECT count(*) FROM int2vector_key_test JOIN int2vector_key_test2 USING (v);
SELECT count(*) FROM int2vector_key_test JOIN int2vector_key_test2 USING (v);
RESET enable_hashjoin;
RESET enable_nestloop;
RESET yb_enable_batchednl;
-- Two-column index: v pinned by = keeps the range on b and the order of b;
-- an IN list on v does not give a merge scan.
DROP INDEX int2vector_key_test_v_idx;
CREATE INDEX int2vector_key_test_v_b_idx ON int2vector_key_test (v ASC, b ASC);
EXPLAIN (COSTS OFF) SELECT k FROM int2vector_key_test WHERE v = '1 2' AND b < 5;
EXPLAIN (COSTS OFF) SELECT k FROM int2vector_key_test WHERE v = '1 2' ORDER BY b;
EXPLAIN (COSTS OFF) SELECT k FROM int2vector_key_test WHERE v IN ('1 2', '2 1') ORDER BY b;
SELECT k FROM int2vector_key_test WHERE v IN ('1 2', '2 1') ORDER BY b;
RESET enable_sort;
RESET enable_incremental_sort;
-- A row comparison led by v and < ANY on v are not index bounds.
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE (v, b) < ('1 2', 0);
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE (v, b) < ('1 2', 0);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < ANY ('{"1 2",0}'::int2vector[]);
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < ANY ('{"1 2",0}'::int2vector[]);
-- DESC index, range primary key, domain.
DROP INDEX int2vector_key_test_v_b_idx;
CREATE INDEX int2vector_key_test_v_desc_idx ON int2vector_key_test (v DESC);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_key_test WHERE v < '1 2';
CREATE TABLE int2vector_pk_test (v int2vector, k int, PRIMARY KEY (v ASC, k));
INSERT INTO int2vector_pk_test SELECT v, k FROM int2vector_key_test WHERE v IS NOT NULL;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_pk_test WHERE v > '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_pk_test WHERE v > '1 2';
CREATE DOMAIN int2vector_domain AS int2vector;
CREATE TABLE int2vector_domain_test (k int PRIMARY KEY, v int2vector_domain);
CREATE INDEX int2vector_domain_test_v_idx ON int2vector_domain_test (v ASC);
INSERT INTO int2vector_domain_test SELECT k, v FROM int2vector_key_test;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_domain_test WHERE v < '1 2';
SELECT array_agg(k ORDER BY k) FROM int2vector_domain_test WHERE v < '1 2';
EXPLAIN (COSTS OFF) SELECT min(v), max(v) FROM int2vector_domain_test;
SELECT min(v), max(v) FROM int2vector_domain_test;
RESET enable_seqscan;
RESET yb_enable_cbo;
DROP TABLE int2vector_key_test;
DROP TABLE int2vector_key_test2;
DROP TABLE int2vector_pk_test;
DROP TABLE int2vector_domain_test;
DROP DOMAIN int2vector_domain;
-- Row comparisons on v are not index bounds either.
CREATE TABLE int2vector_rowcmp_test (k int PRIMARY KEY, v int2vector, b int);
CREATE INDEX int2vector_rowcmp_test_v_idx ON int2vector_rowcmp_test (v ASC);
INSERT INTO int2vector_rowcmp_test VALUES
  (1, '', 1), (2, '1', 2), (3, '1 2', 3), (4, '2 1', 4), (5, '1 2 3', 5),
  (6, '0', 6), (7, '-1', 7), (8, '10', 8), (9, '-1 0', 9), (10, '1 -5', 10),
  (11, '0 0 0', 11), (12, '1 3', 12);
SET enable_seqscan = off;
SET yb_enable_cbo = on;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, k) < ('1 2', 100);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, k) < ('1 2', 100);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, k) > ('1 2', 0);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, k) > ('1 2', 0);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE ('1 2', 100) > (v, k);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, b) < ('1 2', 100);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, b) < ('1 2', 100);
PREPARE int2vector_rowcmp_stmt(int2vector) AS
  SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, b) < ($1, 100);
SET plan_cache_mode = force_generic_plan;
EXECUTE int2vector_rowcmp_stmt('1 2');
RESET plan_cache_mode;
DEALLOCATE int2vector_rowcmp_stmt;
-- v as the second column of the row comparison.
CREATE INDEX int2vector_rowcmp_test_b_v_idx ON int2vector_rowcmp_test (b ASC, v ASC);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (b, v) <= (9, '-1');
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (b, v) <= (9, '-1');
DROP INDEX int2vector_rowcmp_test_b_v_idx;
DROP INDEX int2vector_rowcmp_test_v_idx;
CREATE INDEX int2vector_rowcmp_test_v_desc_idx ON int2vector_rowcmp_test (v DESC);
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, b) > ('1 2', 0);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_test WHERE (v, b) > ('1 2', 0);
CREATE TABLE int2vector_rowcmp_pk_test (v int2vector, k int, b int, PRIMARY KEY (v ASC, k));
INSERT INTO int2vector_rowcmp_pk_test SELECT v, k, b FROM int2vector_rowcmp_test;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_pk_test WHERE (v, b) < ('1 2', 100);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_pk_test WHERE (v, b) < ('1 2', 100);
CREATE DOMAIN int2vector_rowcmp_domain AS int2vector;
CREATE TABLE int2vector_rowcmp_domain_test (k int PRIMARY KEY, v int2vector_rowcmp_domain, b int);
CREATE INDEX int2vector_rowcmp_domain_test_v_idx ON int2vector_rowcmp_domain_test (v ASC);
INSERT INTO int2vector_rowcmp_domain_test SELECT k, v, b FROM int2vector_rowcmp_test;
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_domain_test WHERE (v, b) < ('1 2', 100);
SELECT array_agg(k ORDER BY k) FROM int2vector_rowcmp_domain_test WHERE (v, b) < ('1 2', 100);
-- A hash column of type int2vector in a yb_hash_code row comparison: '1 2' and
-- '-1 24233' share a hash code and sort differently by bytes and by array order.
CREATE TABLE int2vector_hash_rowcmp_test (k int PRIMARY KEY, v int2vector);
CREATE INDEX int2vector_hash_rowcmp_test_v_idx ON int2vector_hash_rowcmp_test (v HASH);
INSERT INTO int2vector_hash_rowcmp_test VALUES (1, '1 2'), (2, '-1 24233'), (3, '2 1'), (4, '10');
EXPLAIN (COSTS OFF) SELECT array_agg(k ORDER BY k) FROM int2vector_hash_rowcmp_test
  WHERE (yb_hash_code(v), v) < (yb_hash_code('1 2'::int2vector), '1 2');
SELECT array_agg(k ORDER BY k) FROM int2vector_hash_rowcmp_test
  WHERE (yb_hash_code(v), v) < (yb_hash_code('1 2'::int2vector), '1 2');
RESET enable_seqscan;
RESET yb_enable_cbo;
DROP TABLE int2vector_rowcmp_test;
DROP TABLE int2vector_rowcmp_pk_test;
DROP TABLE int2vector_rowcmp_domain_test;
DROP TABLE int2vector_hash_rowcmp_test;
DROP DOMAIN int2vector_rowcmp_domain;
