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

-- int2vector keys that already exist (created before int2vector stopped being
-- a key type, or by a restore with yb_binary_restore, as here) can still be
-- truncated and rewritten, since that keeps their types.
SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = true;
SET yb_ignore_relfilenode_ids = true;
SELECT binary_upgrade_set_next_pg_type_oid('1000001'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000002'::oid);
CREATE TABLE int2vector_old_pk (a int2vector, b int, t text, PRIMARY KEY (a ASC));
SELECT binary_upgrade_set_next_pg_type_oid('1000003'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000004'::oid);
CREATE TABLE int2vector_old_idx (id int PRIMARY KEY, a int2vector, t text);
CREATE INDEX int2vector_old_idx_a ON int2vector_old_idx (a ASC);
CREATE INDEX int2vector_old_idx_a_hash ON int2vector_old_idx (a HASH);
RESET yb_binary_restore;
RESET yb_ignore_pg_class_oids;
RESET yb_ignore_relfilenode_ids;
INSERT INTO int2vector_old_pk VALUES ('1 2', 1, '1'), ('10', 2, '2');
INSERT INTO int2vector_old_idx VALUES (1, '1 2', '1'), (2, '10', '2');
TRUNCATE int2vector_old_pk, int2vector_old_idx;
INSERT INTO int2vector_old_pk VALUES ('1 2', 1, '1'), ('10', 2, '2'), ('0', 3, '3');
INSERT INTO int2vector_old_idx VALUES (1, '1 2', '1'), (2, '10', '2'), (3, '0', '3');
-- Volatile default: rewrites the table and rebuilds its indexes.
ALTER TABLE int2vector_old_pk ADD COLUMN c int DEFAULT (random() * 0)::int;
ALTER TABLE int2vector_old_idx ADD COLUMN c int DEFAULT (random() * 0)::int;
-- Type change of a non-key column: also a rewrite.
ALTER TABLE int2vector_old_pk ALTER COLUMN b TYPE bigint;
SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_old_idx WHERE a = '10'::int2vector;
SELECT id FROM int2vector_old_idx WHERE a = '10'::int2vector;
RESET enable_seqscan;
SELECT b FROM int2vector_old_pk WHERE a = '10'::int2vector;
SELECT count(*) FROM int2vector_old_pk;
SELECT count(*) FROM int2vector_old_idx;
-- Not allowed: these define a new int2vector key.
CREATE INDEX int2vector_old_idx_t ON int2vector_old_idx (t ASC);
ALTER TABLE int2vector_old_idx ALTER COLUMN t TYPE int2vector USING t::int2vector;
ALTER TABLE int2vector_old_idx ADD COLUMN d int DEFAULT (random() * 0)::int,
  ADD CONSTRAINT int2vector_old_idx_a_key UNIQUE (a);
CREATE TABLE int2vector_text_pk (t text, PRIMARY KEY (t ASC));
ALTER TABLE int2vector_text_pk ALTER COLUMN t TYPE int2vector USING t::int2vector;
CREATE TABLE int2vector_no_pk (a int2vector);
ALTER TABLE int2vector_no_pk ADD PRIMARY KEY (a);
\d int2vector_old_idx
DROP TABLE int2vector_old_pk, int2vector_old_idx, int2vector_text_pk,
  int2vector_no_pk;
