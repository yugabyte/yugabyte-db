-- Test Index Only Scan with decoded PK columns decoded from ybidxbasectid.
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (COSTS OFF)'
SET yb_enable_cbo = on;
SET yb_enable_primary_key_decode_from_index = on;

-- Test: hash sharded single-column primary key
CREATE TABLE t_hash(id bigserial PRIMARY KEY, v text);
INSERT INTO t_hash(v) SELECT 'val_' || i FROM generate_series(1, 80) i;
CREATE INDEX t_hash_v ON t_hash(v);
ANALYZE t_hash;

SELECT $$
:P SELECT id FROM t_hash WHERE v = 'val_1';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id, v FROM t_hash WHERE v = 'val_3';
$$ AS query \gset
\i :iter_P2

:explain SELECT v FROM t_hash WHERE v = 'val_1';

-- Test: fallback to another scan type for non-PK/non-index columns
ALTER TABLE t_hash ADD COLUMN extra int DEFAULT 0;
:explain SELECT id, extra FROM t_hash WHERE v = 'val_1';

-- Test: range-sharded PK, range scan, primary key scan unchanged
CREATE TABLE t_range(id int, v text, PRIMARY KEY(id ASC));
INSERT INTO t_range SELECT i, 'val_' || i FROM generate_series(1, 100) i;
CREATE INDEX t_range_v ON t_range(v ASC);
ANALYZE t_range;

SELECT $$
:P SELECT id FROM t_range WHERE v = 'val_5';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id, v FROM t_range WHERE v >= 'val_10' AND v < 'val_15' ORDER BY v;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id FROM t_range WHERE id = 5;
$$ AS query \gset
\i :iter_P2

-- Test: composite primary key (multi-column)
CREATE TABLE t_composite(a int, b int, c text, v text, PRIMARY KEY(a, b ASC));
INSERT INTO t_composite SELECT i / 10, i % 10, 'c_' || i, 'v_' || i FROM generate_series(1, 80) i;
CREATE INDEX t_composite_v ON t_composite(v);
ANALYZE t_composite;

SELECT $$
:P SELECT a, b FROM t_composite WHERE v = 'v_15';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT a, b, v FROM t_composite WHERE v = 'v_15';
$$ AS query \gset
\i :iter_P2

:explain SELECT a, b, c FROM t_composite WHERE v = 'v_15';

-- Test: primary key column already included in secondary index
CREATE TABLE t_pk_in_idx(id int PRIMARY KEY, v1 int, v2 int);
INSERT INTO t_pk_in_idx SELECT i, i * 10, i * 100 FROM generate_series(1, 80) i;
CREATE INDEX t_pk_in_idx_v1_id ON t_pk_in_idx(v1, id);
ANALYZE t_pk_in_idx;

SELECT $$
:P SELECT id FROM t_pk_in_idx WHERE v1 = 100;
$$ AS query \gset
\i :iter_P2

:explain SELECT id, v2 FROM t_pk_in_idx WHERE v1 = 100;

-- Test: unique secondary index
CREATE TABLE t_uniq(id int PRIMARY KEY, email text UNIQUE, name text);
INSERT INTO t_uniq SELECT i, 'user' || i || '@test.com', 'name_' || i FROM generate_series(1, 80) i;
ANALYZE t_uniq;

SELECT $$
:P SELECT id FROM t_uniq WHERE email = 'user5@test.com';
$$ AS query \gset
\i :iter_P2

:explain SELECT id, name FROM t_uniq WHERE email = 'user5@test.com';

-- Test: partial index
CREATE TABLE t_partial(id int PRIMARY KEY, status text, val int);
INSERT INTO t_partial SELECT i, CASE WHEN i % 2 = 0 AND i <= 20 THEN 'active' ELSE 'inactive' END, i
    FROM generate_series(1, 100) i;
CREATE INDEX t_partial_val ON t_partial(val) WHERE status = 'active';
ANALYZE t_partial;

:explain SELECT id FROM t_partial WHERE val > 15 AND status = 'active';
SELECT id FROM t_partial WHERE val > 15 AND status = 'active' ORDER BY id;

:explain SELECT id FROM t_partial WHERE val > 15;

-- Test: multi-column secondary index, multiple rows returned
CREATE TABLE t_multi_col(id int PRIMARY KEY, a int, b int, c int);
INSERT INTO t_multi_col SELECT i, i % 5, (i - 60) % 10, i FROM generate_series(1, 80) i;
CREATE INDEX t_multi_col_ab ON t_multi_col(a, b ASC);
ANALYZE t_multi_col;

SELECT $$
:P SELECT id FROM t_multi_col WHERE a = 2 AND b = 2;
$$ AS query \gset
\i :iter_P2

:explain SELECT id, a, b FROM t_multi_col WHERE a = 2;

:explain SELECT id, c FROM t_multi_col WHERE a = 2 AND b = 2;

SELECT id FROM t_multi_col WHERE a = 3 ORDER BY id;

-- Test: hash-sharded composite primary key
CREATE TABLE t_hash_comp(h1 int, h2 int, v int, PRIMARY KEY((h1, h2) HASH));
INSERT INTO t_hash_comp SELECT i, i + 100, i * 10 FROM generate_series(1, 80) i;
CREATE INDEX t_hash_comp_v ON t_hash_comp(v);
ANALYZE t_hash_comp;

SELECT $$
:P SELECT h1, h2 FROM t_hash_comp WHERE v = 50;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT h1, h2, v FROM t_hash_comp WHERE v = 50;
$$ AS query \gset
\i :iter_P2

-- Test: mixed hash + range composite primary key
CREATE TABLE t_mixed_pk(h int, r int, v text, PRIMARY KEY(h HASH, r ASC));
INSERT INTO t_mixed_pk SELECT i / 5, i, 'v_' || i FROM generate_series(1, 80) i;
CREATE INDEX t_mixed_pk_v ON t_mixed_pk(v ASC);
ANALYZE t_mixed_pk;

SELECT $$
:P SELECT h, r FROM t_mixed_pk WHERE v = 'v_7';
$$ AS query \gset
\i :iter_P2

-- Test: filter on decoded PK column (pushdown disabled)
SELECT $$
:P SELECT id FROM t_range WHERE v = 'val_5' AND id % 2 = 1;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id, v FROM t_range WHERE v >= 'val_10' AND v < 'val_20' AND id % 3 = 1 ORDER BY v;
$$ AS query \gset
\i :iter_P2

-- Test: aggregates on decoded PK columns (pushdown disabled)
SELECT $$
:P SELECT COUNT(id) FROM t_range WHERE v >= 'val_10' AND v < 'val_20';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT MIN(id), MAX(id) FROM t_range WHERE v >= 'val_10' AND v < 'val_20';
$$ AS query \gset
\i :iter_P2

-- Test: filters on physical columns still push down
SELECT $$
:P SELECT id FROM t_range WHERE v >= 'val_10' AND v < 'val_15';
$$ AS query \gset
\i :iter_P2

-- Test: aggregates on physical index columns still push down
SELECT $$
:P SELECT COUNT(*) FROM t_range WHERE v >= 'val_10' AND v < 'val_20';
$$ AS query \gset
\i :iter_P2

-- Test: NULL index column with primary key decoded
CREATE TABLE t_null_idx(id int PRIMARY KEY, v int);
INSERT INTO t_null_idx SELECT i, CASE WHEN i % 2 = 1 and i <= 4 THEN NULL ELSE i * 5 END
    FROM generate_series(1, 80) i;
CREATE INDEX t_null_idx_v ON t_null_idx(v ASC);
ANALYZE t_null_idx;

:explain SELECT id FROM t_null_idx WHERE v IS NULL;
SELECT id FROM t_null_idx WHERE v IS NULL ORDER BY id;

SELECT $$
:P SELECT id FROM t_null_idx WHERE v = 10;
$$ AS query \gset
\i :iter_P2

-- Test: different primary key data types
CREATE TABLE t_text_pk(id text PRIMARY KEY, v int);
INSERT INTO t_text_pk SELECT 'key_' || i, i FROM generate_series(1, 80) i;
CREATE INDEX t_text_pk_v ON t_text_pk(v);
ANALYZE t_text_pk;

SELECT $$
:P SELECT id FROM t_text_pk WHERE v = 3;
$$ AS query \gset
\i :iter_P2

CREATE TABLE t_uuid_pk(id uuid PRIMARY KEY DEFAULT gen_random_uuid(), v int);
INSERT INTO t_uuid_pk(v) SELECT i FROM generate_series(1, 80) i;
CREATE INDEX t_uuid_pk_v ON t_uuid_pk(v);
ANALYZE t_uuid_pk;

:explain SELECT id FROM t_uuid_pk WHERE v = 3;

CREATE TABLE t_float_pk(id float4 PRIMARY KEY, v int);
INSERT INTO t_float_pk SELECT i * 1.5, i FROM generate_series(1, 80) i;
CREATE INDEX t_float_pk_v ON t_float_pk(v);
ANALYZE t_float_pk;

SELECT $$
:P SELECT id FROM t_float_pk WHERE v = 3;
$$ AS query \gset
\i :iter_P2

CREATE TABLE t_double_pk(id float8 PRIMARY KEY, v int);
INSERT INTO t_double_pk SELECT i * 1.1, i FROM generate_series(1, 80) i;
CREATE INDEX t_double_pk_v ON t_double_pk(v);
ANALYZE t_double_pk;

SELECT $$
:P SELECT id FROM t_double_pk WHERE v = 3;
$$ AS query \gset
\i :iter_P2

CREATE TABLE t_ts_pk(id timestamp PRIMARY KEY, v int);
INSERT INTO t_ts_pk SELECT '2024-01-01'::timestamp + (i || ' hours')::interval, i
    FROM generate_series(1, 80) i;
CREATE INDEX t_ts_pk_v ON t_ts_pk(v);
ANALYZE t_ts_pk;

SELECT $$
:P SELECT id FROM t_ts_pk WHERE v = 3;
$$ AS query \gset
\i :iter_P2

CREATE TABLE t_bool_pk(id boolean, v int, extra int, PRIMARY KEY(id, extra));
INSERT INTO t_bool_pk SELECT i % 2 = 1, i, i FROM generate_series(1, 80) i;
CREATE INDEX t_bool_pk_v ON t_bool_pk(v);
ANALYZE t_bool_pk;

SELECT $$
:P SELECT id, extra FROM t_bool_pk WHERE v = 3;
$$ AS query \gset
\i :iter_P2

-- Test: Join where the join key is a decoded decoded PK column.
CREATE TABLE t_join_parent(tag_id int, label text, PRIMARY KEY(tag_id ASC));
INSERT INTO t_join_parent SELECT i, 'label_' || i FROM generate_series(1, 500) i;
ANALYZE t_join_parent;

CREATE TABLE t_join_child(item_id int, tag int, name text, PRIMARY KEY(item_id ASC));
INSERT INTO t_join_child SELECT i, i % 50, 'name_' || i FROM generate_series(1, 1000) i;
CREATE INDEX t_join_child_tag ON t_join_child(tag ASC);
ANALYZE t_join_child;

SELECT $$
:P SELECT i.item_id, t.label FROM t_join_child i JOIN t_join_parent t ON i.item_id = t.tag_id WHERE i.tag = 1 ORDER BY i.item_id;
$$ AS query \gset
\i :iter_P2

-- Test: join with filter on decoded PK column (pushdown disabled)
SELECT $$
:P SELECT i.item_id, t.label FROM t_join_child i JOIN t_join_parent t ON i.item_id = t.tag_id WHERE i.tag = 1 AND i.item_id % 500 = 1 ORDER BY i.item_id;
$$ AS query \gset
\i :iter_P2

-- Test: optimizer should prefer INCLUDE index over decoded pk index due to pushdown limitations
CREATE TABLE t_include_cmp(id int, v int, pad text, PRIMARY KEY(id ASC));
INSERT INTO t_include_cmp SELECT i, i % 100, repeat('x', 20)
    FROM generate_series(1, 10000) i;
CREATE INDEX idx_decoded_pk ON t_include_cmp(v ASC);
CREATE INDEX idx_include_pk ON t_include_cmp(v ASC) INCLUDE (id);
ANALYZE t_include_cmp;

SELECT $$
:P SELECT id FROM t_include_cmp WHERE v = 50 AND id > 5000;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT COUNT(id) FROM t_include_cmp WHERE v = 50;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT COUNT(id) FROM t_include_cmp WHERE v >= 20 AND v < 80;
$$ AS query \gset
\i :iter_P2

-- Test: partial INCLUDE with both decoded and included pk columns.
CREATE TABLE t_partial_include(h1 int, h2 int, v int, extra text, PRIMARY KEY(h1 ASC, h2 ASC));
INSERT INTO t_partial_include
    SELECT (i / 100) + 1, i, i % 50, 'pad_' || i FROM generate_series(1, 1000) i;
CREATE INDEX t_partial_include_v ON t_partial_include(v ASC) INCLUDE (h1);
ANALYZE t_partial_include;

SELECT $$
:P SELECT h1, h2 FROM t_partial_include WHERE v = 25 AND h1 > 5 AND h2 > 800 ORDER BY h2;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT COUNT(h1), COUNT(h2) FROM t_partial_include WHERE v >= 20 AND v < 30;
$$ AS query \gset
\i :iter_P2

-- Test: join on table with included pk column
SELECT $$
:P SELECT c.h1, c.h2, p.label FROM t_partial_include c JOIN t_join_parent p ON c.h1 = p.tag_id WHERE c.v = 25 ORDER BY c.h2;
$$ AS query \gset
\i :iter_P2

-- Test: expression index
CREATE TABLE t_expr(id int PRIMARY KEY, name text);
INSERT INTO t_expr SELECT i, 'Name_' || i FROM generate_series(1, 80) i;
CREATE INDEX t_expr_lower ON t_expr(lower(name));
ANALYZE t_expr;

SELECT $$
:P SELECT id FROM t_expr WHERE lower(name) = 'name_5';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id, lower(name) FROM t_expr WHERE lower(name) = 'name_3';
$$ AS query \gset
\i :iter_P2

-- Test: IN queries with decoded PK
SELECT $$
:P SELECT id FROM t_range WHERE v IN ('val_3', 'val_7', 'val_15') ORDER BY id;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id FROM t_multi_col WHERE a IN (1, 3) AND b = 2 ORDER BY id;
$$ AS query \gset
\i :iter_P2

-- Test: LIKE queries with decoded PK
SELECT $$
:P SELECT id FROM t_range WHERE v LIKE 'val\_1%' ESCAPE '\' ORDER BY v;
$$ AS query \gset
\i :iter_P2

-- Test: partial index with composite columns
CREATE TABLE t_partial_comp(id int PRIMARY KEY, cat text, sub int, val int);
INSERT INTO t_partial_comp
    SELECT i, CASE WHEN i % 3 = 0 THEN 'A' WHEN i % 3 = 1 THEN 'B' ELSE 'C' END,
           i % 5, i
    FROM generate_series(1, 80) i;
CREATE INDEX t_partial_comp_idx ON t_partial_comp(sub, val ASC) WHERE cat = 'A';
ANALYZE t_partial_comp;

SELECT $$
:P SELECT id FROM t_partial_comp WHERE cat = 'A' AND sub = 2 AND val > 10 ORDER BY id;
$$ AS query \gset
\i :iter_P2

:explain SELECT id, val FROM t_partial_comp WHERE cat = 'A' AND sub = 0;

-- Test: index on jsonb key expression
CREATE TABLE t_jsonb_expr(id int PRIMARY KEY, data jsonb);
INSERT INTO t_jsonb_expr SELECT i, jsonb_build_object('key', i, 'val', 'x')
    FROM generate_series(1, 500) i;
CREATE INDEX t_jsonb_expr_key ON t_jsonb_expr(((data->>'key')::int));
ANALYZE t_jsonb_expr;

SELECT $$
:P SELECT id FROM t_jsonb_expr WHERE (data->>'key')::int = 5;
$$ AS query \gset
\i :iter_P2

-- Test: temporary table should not use decoded PK
CREATE TEMP TABLE t_temp(id int PRIMARY KEY, v int);
INSERT INTO t_temp SELECT i, i * 10 FROM generate_series(1, 80) i;
CREATE INDEX t_temp_v ON t_temp(v);
ANALYZE t_temp;

SELECT $$
:P SELECT id FROM t_temp WHERE v = 50;
$$ AS query \gset
\i :iter_P2

DROP TABLE t_temp;

-- Test: GIN index should not use decoded PK
CREATE TABLE t_gin(id int PRIMARY KEY, tags jsonb);
INSERT INTO t_gin SELECT i, ('["tag_' || i || '"]')::jsonb FROM generate_series(1, 80) i;
CREATE INDEX t_gin_tags ON t_gin USING gin(tags);
ANALYZE t_gin;

:explain SELECT id FROM t_gin WHERE tags @> '"tag_5"';

-- Test: vector index should not use decoded PK
CREATE EXTENSION IF NOT EXISTS vector;
CREATE TABLE t_vector(id int PRIMARY KEY, emb vector(3));
INSERT INTO t_vector SELECT i, ('[' || i || ',0,0]')::vector FROM generate_series(1, 80) i;
CREATE INDEX t_vector_emb ON t_vector USING ybhnsw (emb vector_l2_ops);

:explain SELECT id FROM t_vector ORDER BY emb <-> '[5,0,0]' LIMIT 3;

-- Test: yb_enable_primary_key_decode_from_index = off disables decoded PK.
SET yb_enable_primary_key_decode_from_index = off;

SELECT $$
:P SELECT id FROM t_range WHERE v = 'val_5';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT a, b FROM t_composite WHERE v = 'v_15';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT COUNT(id) FROM t_range WHERE v >= 'val_10' AND v < 'val_20';
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT id FROM t_range WHERE v = 'val_5' AND id % 2 = 1;
$$ AS query \gset
\i :iter_P2

SELECT $$
:P SELECT i.item_id, t.label FROM t_join_child i JOIN t_join_parent t ON i.item_id = t.tag_id WHERE i.tag = 1 ORDER BY i.item_id;
$$ AS query \gset
\i :iter_P2

-- Cleanup non-colocated tables before switching database.
DROP TABLE t_expr CASCADE;
DROP TABLE t_partial_comp CASCADE;
DROP TABLE t_include_cmp CASCADE;
DROP TABLE t_partial_include CASCADE;
DROP TABLE t_hash CASCADE;
DROP TABLE t_range CASCADE;
DROP TABLE t_composite CASCADE;
DROP TABLE t_pk_in_idx CASCADE;
DROP TABLE t_uniq CASCADE;
DROP TABLE t_partial CASCADE;
DROP TABLE t_multi_col CASCADE;
DROP TABLE t_hash_comp CASCADE;
DROP TABLE t_mixed_pk CASCADE;
DROP TABLE t_null_idx CASCADE;
DROP TABLE t_text_pk CASCADE;
DROP TABLE t_uuid_pk CASCADE;
DROP TABLE t_float_pk CASCADE;
DROP TABLE t_double_pk CASCADE;
DROP TABLE t_ts_pk CASCADE;
DROP TABLE t_bool_pk CASCADE;
DROP TABLE t_gin CASCADE;
DROP TABLE t_vector CASCADE;
DROP EXTENSION vector;
DROP TABLE t_jsonb_expr CASCADE;
DROP TABLE t_join_parent CASCADE;
DROP TABLE t_join_child CASCADE;

-- Test: colocated tables with decoded PK
CREATE DATABASE colo_ios_test WITH COLOCATION = true;
\c colo_ios_test
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (COSTS OFF)'
SET yb_enable_cbo = on;
SET yb_enable_primary_key_decode_from_index = on;

CREATE TABLE t_colocated(id int PRIMARY KEY, v int, extra text);
INSERT INTO t_colocated SELECT i, i % 20, 'pad' FROM generate_series(1, 100) i;
CREATE INDEX t_colocated_v ON t_colocated(v ASC);
ANALYZE t_colocated;

-- Test: Index Only Scan with decoded PK on colocated table.
SELECT $$
:P SELECT id FROM t_colocated WHERE v = 5;
$$ AS query \gset
\i :iter_P2

-- Test: range scan on colocated table.
SELECT $$
:P SELECT id, v FROM t_colocated WHERE v >= 10 AND v < 15 ORDER BY v;
$$ AS query \gset
\i :iter_P2

-- Test: filter on decoded PK should not push down on colocated table.
:explain SELECT id FROM t_colocated WHERE v = 5 AND id > 50;

-- Test: fallback to another scan type when non-index column is needed.
:explain SELECT id, extra FROM t_colocated WHERE v = 5;

DROP TABLE t_colocated CASCADE;
