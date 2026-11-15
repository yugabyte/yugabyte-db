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

-- int2vector keys: DocDB would order them by bytes, not by array_cmp.
CREATE TABLE int2vector_test (id int PRIMARY KEY, a int2vector);
INSERT INTO int2vector_test VALUES (1, ''), (2, '1'), (3, '1 2'), (4, '2 1'),
  (5, '1 2 3'), (6, '0'), (7, '-1'), (8, '10');
EXPLAIN (COSTS OFF) SELECT min(a), max(a) FROM int2vector_test;
SELECT min(a), max(a) FROM int2vector_test;
SELECT yb_hash_code('1 2'::int2vector);
CREATE INDEX int2vector_test_a ON int2vector_test (a ASC);
CREATE INDEX int2vector_test_a_desc ON int2vector_test (a DESC);
CREATE INDEX int2vector_test_a_hash ON int2vector_test (a HASH);
CREATE TABLE int2vector_pk (a int2vector, PRIMARY KEY (a ASC));
CREATE DOMAIN int2vector_domain AS int2vector;
CREATE TABLE int2vector_domain_test (a int2vector_domain, PRIMARY KEY (a ASC));
SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_test WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_test WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_test WHERE a > '1 2'::int2vector;
RESET enable_seqscan;
-- Still allowed: GIN keys are int2, INCLUDE columns are not keys.
CREATE INDEX int2vector_test_gin ON int2vector_test USING ybgin (a);
CREATE INDEX int2vector_test_id_incl ON int2vector_test (id ASC) INCLUDE (a);
DROP TABLE int2vector_test;
DROP TABLE IF EXISTS int2vector_pk, int2vector_domain_test;
DROP DOMAIN int2vector_domain;
