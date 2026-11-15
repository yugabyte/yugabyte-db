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

-- Rebuilding an index or primary key keeps an existing int2vector key whose
-- type does not change. Under a restore, other non-key types stay rejected.
SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = true;
SET yb_ignore_relfilenode_ids = true;
SELECT binary_upgrade_set_next_pg_type_oid('1000005'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000006'::oid);
CREATE TABLE int2vector_old_mix (id int PRIMARY KEY, a int2vector, b int);
CREATE INDEX int2vector_old_mix_ab ON int2vector_old_mix (a ASC, b);
CREATE INDEX int2vector_old_mix_incl ON int2vector_old_mix (a ASC) INCLUDE (b);
CREATE INDEX int2vector_old_mix_part ON int2vector_old_mix (a ASC) WHERE b > 0;
SELECT binary_upgrade_set_next_pg_type_oid('1000007'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000008'::oid);
CREATE TABLE int2vector_old_pk2 (a int2vector, b int, PRIMARY KEY (a ASC, b));
SELECT binary_upgrade_set_next_pg_type_oid('1000009'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000010'::oid);
CREATE TABLE int2vector_old_uniq (id int PRIMARY KEY, a int2vector);
CREATE UNIQUE INDEX int2vector_old_uniq_a ON int2vector_old_uniq (a ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000011'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000012'::oid);
CREATE TABLE int2vector_old_part (a int2vector, b int, c int)
  PARTITION BY RANGE (b);
SELECT binary_upgrade_set_next_pg_type_oid('1000013'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000014'::oid);
CREATE TABLE int2vector_old_part1 PARTITION OF int2vector_old_part
  FOR VALUES FROM (0) TO (100);
CREATE INDEX int2vector_old_part_ac ON int2vector_old_part (a ASC, c);
SELECT binary_upgrade_set_next_pg_type_oid('1000015'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000016'::oid);
CREATE TABLE int2array_restore (a int2[]);
CREATE INDEX int2array_restore_a ON int2array_restore (a ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000017'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000018'::oid);
CREATE TABLE int2array_restore_pk (a int2[], PRIMARY KEY (a ASC));
RESET yb_binary_restore;
RESET yb_ignore_pg_class_oids;
RESET yb_ignore_relfilenode_ids;
INSERT INTO int2vector_old_mix VALUES (1, '1 2', 1), (2, '10', 2);
INSERT INTO int2vector_old_pk2 VALUES ('1 2', 1), ('10', 2);
INSERT INTO int2vector_old_uniq VALUES (1, '1 2'), (2, '10');
INSERT INTO int2vector_old_part VALUES ('1 2', 1, 1), ('10', 2, 2);
-- Type change of a non-key column that an int2vector index or PK references.
ALTER TABLE int2vector_old_mix ALTER COLUMN b TYPE bigint;
ALTER TABLE int2vector_old_pk2 ALTER COLUMN b TYPE bigint;
ALTER TABLE int2vector_old_part ALTER COLUMN c TYPE bigint;
\d int2vector_old_mix
\d int2vector_old_pk2
\d int2vector_old_part1
SELECT b FROM int2vector_old_pk2 WHERE a = '10'::int2vector;
-- With rewrites off, ALTER COLUMN TYPE copies the table and all its indexes.
SET yb_enable_alter_table_rewrite = off;
ALTER TABLE int2vector_old_mix ALTER COLUMN b TYPE smallint;
RESET yb_enable_alter_table_rewrite;
\d int2vector_old_mix
-- Not allowed: a new primary key from an existing int2vector index.
ALTER TABLE int2vector_old_uniq DROP CONSTRAINT int2vector_old_uniq_pkey;
ALTER TABLE int2vector_old_uniq ADD PRIMARY KEY USING INDEX int2vector_old_uniq_a;
\d int2vector_old_uniq
SELECT id FROM int2vector_old_uniq ORDER BY id;
DROP TABLE int2vector_old_mix, int2vector_old_pk2, int2vector_old_uniq,
  int2vector_old_part, int2array_restore;
DROP TABLE IF EXISTS int2array_restore_pk;

-- An existing int2vector key serves only equality: DocDB orders it by bytes,
-- not by array_cmp. Range quals and row comparisons on it are filters, and
-- index order stops before it.
SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = true;
SET yb_ignore_relfilenode_ids = true;
SELECT binary_upgrade_set_next_pg_type_oid('1000019'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000020'::oid);
CREATE TABLE int2vector_rs_idx (id int PRIMARY KEY, a int2vector);
CREATE INDEX int2vector_rs_idx_a ON int2vector_rs_idx (a ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000021'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000022'::oid);
CREATE TABLE int2vector_rs_desc (id int PRIMARY KEY, a int2vector);
CREATE INDEX int2vector_rs_desc_a ON int2vector_rs_desc (a DESC);
SELECT binary_upgrade_set_next_pg_type_oid('1000023'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000024'::oid);
CREATE TABLE int2vector_rs_pk (a int2vector, id int, PRIMARY KEY (a ASC, id));
SELECT binary_upgrade_set_next_pg_type_oid('1000025'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000026'::oid);
CREATE DOMAIN int2vector_rs_dom AS int2vector;
SELECT binary_upgrade_set_next_pg_type_oid('1000027'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000028'::oid);
CREATE TABLE int2vector_rs_dom_pk (a int2vector_rs_dom, id int,
  PRIMARY KEY (a ASC));
SELECT binary_upgrade_set_next_pg_type_oid('1000029'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000030'::oid);
CREATE TABLE int2vector_rs_ida (id int, a int2vector, PRIMARY KEY (id ASC, a ASC));
SELECT binary_upgrade_set_next_pg_type_oid('1000031'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000032'::oid);
CREATE TABLE int2vector_rs_ida2 (k int PRIMARY KEY, id int, a int2vector);
CREATE INDEX int2vector_rs_ida2_id_a ON int2vector_rs_ida2 (id ASC, a ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000033'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000034'::oid);
CREATE TABLE int2vector_rs_hash (id int PRIMARY KEY, a int2vector);
CREATE INDEX int2vector_rs_hash_a_id ON int2vector_rs_hash (a HASH, id ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000035'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000036'::oid);
CREATE TABLE int2vector_rs_ha (id int PRIMARY KEY, h int, a int2vector);
CREATE INDEX int2vector_rs_ha_h_a ON int2vector_rs_ha (h HASH, a ASC);
RESET yb_binary_restore;
RESET yb_ignore_pg_class_oids;
RESET yb_ignore_relfilenode_ids;
-- xCluster DDL replay runs DDL that its source already accepted, so it keeps
-- int2vector keys too.
SET yb_xcluster_automatic_mode_target_ddl = true;
CREATE TABLE int2vector_xc (id int PRIMARY KEY, a int2vector);
CREATE INDEX int2vector_xc_a ON int2vector_xc (a ASC);
CREATE TABLE int2vector_xc_pk (a int2vector, id int, PRIMARY KEY (a ASC));
RESET yb_xcluster_automatic_mode_target_ddl;
CREATE INDEX int2vector_xc_a_desc ON int2vector_xc (a DESC);
INSERT INTO int2vector_rs_idx VALUES (1, ''), (2, '1'), (3, '1 2'), (4, '2 1'),
  (5, '1 2 3'), (6, '0'), (7, '-1'), (8, '10');
INSERT INTO int2vector_rs_desc SELECT id, a FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_pk SELECT a, id FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_dom_pk SELECT a, id FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_ida SELECT 1, a FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_ida VALUES (0, '10'), (2, '0');
INSERT INTO int2vector_rs_ida2 SELECT row_number() OVER (), id, a
  FROM int2vector_rs_ida;
INSERT INTO int2vector_xc SELECT id, a FROM int2vector_rs_idx;
INSERT INTO int2vector_xc_pk SELECT a, id FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_hash SELECT id, a FROM int2vector_rs_idx;
INSERT INTO int2vector_rs_hash VALUES (9, '1 2'), (10, '1 2');
INSERT INTO int2vector_rs_ha SELECT id, 5, a FROM int2vector_rs_idx;
SET enable_seqscan = off;
-- Range quals on an int2vector key column.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_idx WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_idx WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_idx WHERE a > '1 2'::int2vector;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_desc WHERE a > '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_desc WHERE a > '1 2'::int2vector;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_pk WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_pk WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_dom_pk WHERE a > '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_xc WHERE a < '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_idx
  WHERE a = ANY ('{"1 2", "10", "0", "2 1"}'::int2vector[]);
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_idx
  WHERE a < ANY ('{"1 2", "0"}'::int2vector[]);
-- Ordering by an int2vector key column.
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_idx ORDER BY a;
SELECT id, a FROM int2vector_rs_idx ORDER BY a;
SELECT id FROM int2vector_rs_desc ORDER BY a DESC;
SELECT id FROM int2vector_rs_pk ORDER BY a;
SELECT id FROM int2vector_rs_dom_pk ORDER BY a;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_xc_pk ORDER BY a;
SELECT id FROM int2vector_xc_pk ORDER BY a;
EXPLAIN (COSTS OFF) SELECT min(a), max(a) FROM int2vector_rs_idx;
SELECT min(a), max(a) FROM int2vector_rs_idx;
-- Row comparisons with the int2vector column first and second.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_pk
  WHERE ROW(a, id) > ROW('1 2'::int2vector, 0);
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_pk
  WHERE ROW(a, id) > ROW('1 2'::int2vector, 0);
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_idx
  WHERE ROW(a, id) > ROW('1 2'::int2vector, 0);
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_ida
  WHERE ROW(id, a) > ROW(1, '1 2'::int2vector);
SELECT id, a FROM int2vector_rs_ida
  WHERE ROW(id, a) > ROW(1, '1 2'::int2vector) ORDER BY id, a;
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_ida2
  WHERE ROW(id, a) > ROW(1, '1 2'::int2vector);
SELECT id, a FROM int2vector_rs_ida2
  WHERE ROW(id, a) > ROW(1, '1 2'::int2vector) ORDER BY id, a;
-- A row led by yb_hash_code also stops before the int2vector column.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_ha
  WHERE ROW(yb_hash_code(h), h, a) > ROW(yb_hash_code(5), 5, '1 2'::int2vector);
SELECT array_agg(id ORDER BY id) FROM int2vector_rs_ha
  WHERE ROW(yb_hash_code(h), h, a) > ROW(yb_hash_code(5), 5, '1 2'::int2vector);
-- The key columns before the int2vector column keep their order.
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_ida ORDER BY id;
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_ida ORDER BY id, a;
SELECT id, a FROM int2vector_rs_ida ORDER BY id, a;
EXPLAIN (COSTS OFF) SELECT id, a FROM int2vector_rs_ida2 ORDER BY id, a;
SELECT id, a FROM int2vector_rs_ida2 ORDER BY id, a;
-- Equality on the int2vector column, and a range on a later column, still use
-- the index.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_idx WHERE a = '10'::int2vector;
SELECT id FROM int2vector_rs_idx WHERE a = '10'::int2vector;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_idx
  WHERE a = ANY ('{"1 2", "10"}'::int2vector[]);
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_pk
  WHERE a = '1 2'::int2vector AND id > 0;
SELECT id FROM int2vector_rs_pk WHERE a = '1 2'::int2vector AND id > 0;
-- Pinned to one value, the int2vector column keeps the order of later columns.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_pk
  WHERE a = '1 2'::int2vector ORDER BY id;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_hash
  WHERE a = '1 2'::int2vector ORDER BY id;
SELECT id FROM int2vector_rs_hash WHERE a = '1 2'::int2vector ORDER BY id;
-- An IN list on it is not bound to DocDB, so it can't serve a merge scan.
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_rs_pk
  WHERE a IN ('1 2', '10', '0') ORDER BY id;
SELECT id FROM int2vector_rs_pk WHERE a IN ('1 2', '10', '0') ORDER BY id;
RESET enable_seqscan;
DROP TABLE int2vector_rs_idx, int2vector_rs_desc, int2vector_rs_pk,
  int2vector_rs_dom_pk, int2vector_rs_ida, int2vector_rs_ida2,
  int2vector_rs_hash, int2vector_rs_ha;
DROP TABLE IF EXISTS int2vector_xc, int2vector_xc_pk;
DROP DOMAIN int2vector_rs_dom;

-- A partition of a partitioned table with an int2vector index or primary key
-- clones that existing key, so it is allowed.
SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = true;
SET yb_ignore_relfilenode_ids = true;
SELECT binary_upgrade_set_next_pg_type_oid('1000037'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000038'::oid);
CREATE TABLE int2vector_pt_idx (id int, a int2vector) PARTITION BY RANGE (id);
CREATE INDEX int2vector_pt_idx_a ON int2vector_pt_idx (a ASC);
SELECT binary_upgrade_set_next_pg_type_oid('1000039'::oid);
SELECT binary_upgrade_set_next_array_pg_type_oid('1000040'::oid);
CREATE TABLE int2vector_pt_pk (id int, a int2vector, PRIMARY KEY (a ASC, id))
  PARTITION BY RANGE (id);
RESET yb_binary_restore;
RESET yb_ignore_pg_class_oids;
RESET yb_ignore_relfilenode_ids;
CREATE TABLE int2vector_pt_idx1 PARTITION OF int2vector_pt_idx
  FOR VALUES FROM (0) TO (5);
CREATE TABLE int2vector_pt_idx2 (id int, a int2vector);
ALTER TABLE int2vector_pt_idx ATTACH PARTITION int2vector_pt_idx2
  FOR VALUES FROM (5) TO (100);
CREATE TABLE int2vector_pt_pk1 PARTITION OF int2vector_pt_pk
  FOR VALUES FROM (0) TO (100);
\d int2vector_pt_idx2
\d int2vector_pt_pk1
INSERT INTO int2vector_pt_idx VALUES (1, ''), (2, '1'), (3, '1 2'), (4, '2 1'),
  (5, '1 2 3'), (6, '0'), (7, '-1'), (8, '10');
INSERT INTO int2vector_pt_pk SELECT id, a FROM int2vector_pt_idx;
SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_pt_idx WHERE a = '10'::int2vector;
SELECT id FROM int2vector_pt_idx WHERE a = '10'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_pt_idx WHERE a < '1 2'::int2vector;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_pt_pk WHERE a > '1 2'::int2vector;
SELECT array_agg(id ORDER BY id) FROM int2vector_pt_pk WHERE a > '1 2'::int2vector;
SELECT id FROM int2vector_pt_pk ORDER BY a;
RESET enable_seqscan;
-- Not allowed: a new int2vector key on a partitioned table.
CREATE INDEX int2vector_pt_idx_a_desc ON int2vector_pt_idx (a DESC);
CREATE TABLE int2vector_pt_new (id int, a int2vector, PRIMARY KEY (a ASC, id))
  PARTITION BY RANGE (id);
DROP TABLE int2vector_pt_idx, int2vector_pt_pk;
DROP TABLE IF EXISTS int2vector_pt_new;

-- yb_hash_code still takes int2vector, which an expression index or a stored
-- generated column may already use. Other non-key types stay rejected.
CREATE TABLE int2vector_hc (id int PRIMARY KEY, a int2vector,
  h int GENERATED ALWAYS AS (yb_hash_code(a)) STORED);
CREATE INDEX int2vector_hc_bucket ON int2vector_hc ((yb_hash_code(a) % 16) ASC);
INSERT INTO int2vector_hc VALUES (1, '1 2'), (2, '10'), (3, '1 2');
SELECT id, h = yb_hash_code(a) AS same_hash FROM int2vector_hc ORDER BY id;
SELECT count(DISTINCT h) FROM int2vector_hc WHERE a = '1 2'::int2vector;
SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT id FROM int2vector_hc
  WHERE yb_hash_code(a) % 16 = yb_hash_code('10'::int2vector) % 16
  AND a = '10'::int2vector;
SELECT id FROM int2vector_hc
  WHERE yb_hash_code(a) % 16 = yb_hash_code('10'::int2vector) % 16
  AND a = '10'::int2vector;
RESET enable_seqscan;
SELECT yb_hash_code('{1,2}'::int2[]);
DROP TABLE int2vector_hc;
