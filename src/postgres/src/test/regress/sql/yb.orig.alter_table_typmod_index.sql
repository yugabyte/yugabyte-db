--
-- Tests to validate rewrite behavior on expression and partial indexes caused by a typmod change.
--
SET client_min_messages TO WARNING;

-- The GUC is off by default on this branch.
SHOW yb_alter_type_reuse_expr_and_partial_indexes;
SET yb_alter_type_reuse_expr_and_partial_indexes = on;

-- Each index's OID and relfilenode before and after an ALTER.
CREATE TEMP TABLE typmod_idx_info (description text, relname name, old_oid oid, old_relfilenode oid, new_oid oid,
    new_relfilenode oid, PRIMARY KEY (description, relname));
CREATE PROCEDURE typmod_idx_snapshot(descr text, idx name) LANGUAGE sql AS $$
    INSERT INTO typmod_idx_info (description, relname, old_oid, old_relfilenode)
    SELECT descr, relname, oid, relfilenode FROM pg_class WHERE relname = idx;
$$;
CREATE PROCEDURE typmod_idx_record(descr text, idx name) LANGUAGE sql AS $$
    UPDATE typmod_idx_info i SET new_oid = c.oid, new_relfilenode = c.relfilenode
    FROM pg_class c WHERE c.relname = idx AND i.relname = idx AND i.description = descr;
$$;
CREATE TEMP VIEW typmod_idx_results AS
    SELECT description, relname,
        CASE WHEN old_oid = new_oid THEN 'reused' ELSE 'changed' END AS oid,
        CASE WHEN old_relfilenode = new_relfilenode THEN 'reused' ELSE 'changed' END AS relfilenode
    FROM typmod_idx_info;

-- Tests to exercise various data types
CREATE TABLE typmod_test (k int PRIMARY KEY, v varchar(10), n numeric(10, 2), ch char(5));
CREATE INDEX NONCONCURRENTLY idx_typmod_v_expr ON typmod_test (lower(v));
CREATE INDEX NONCONCURRENTLY idx_typmod_v_part ON typmod_test (k) WHERE v <> '';
CREATE INDEX NONCONCURRENTLY idx_typmod_n_expr ON typmod_test ((n + 1));
CREATE INDEX NONCONCURRENTLY idx_typmod_n_part ON typmod_test (k) WHERE n > 0;
CREATE INDEX NONCONCURRENTLY idx_typmod_ch_expr ON typmod_test (lower(ch));
CREATE INDEX NONCONCURRENTLY idx_typmod_ch_part ON typmod_test (k) WHERE ch <> '';
INSERT INTO typmod_test VALUES (1, 'Abc', 1.5, 'Xy');
CALL typmod_idx_snapshot('varchar(10) -> varchar(20)', 'idx_typmod_v_expr');
CALL typmod_idx_snapshot('varchar(10) -> varchar(20)', 'idx_typmod_v_part');
ALTER TABLE typmod_test ALTER COLUMN v TYPE varchar(20);
CALL typmod_idx_record('varchar(10) -> varchar(20)', 'idx_typmod_v_expr');
CALL typmod_idx_record('varchar(10) -> varchar(20)', 'idx_typmod_v_part');
CALL typmod_idx_snapshot('varchar(20) -> varchar(15)', 'idx_typmod_v_expr');
CALL typmod_idx_snapshot('varchar(20) -> varchar(15)', 'idx_typmod_v_part');
ALTER TABLE typmod_test ALTER COLUMN v TYPE varchar(15);
CALL typmod_idx_record('varchar(20) -> varchar(15)', 'idx_typmod_v_expr');
CALL typmod_idx_record('varchar(20) -> varchar(15)', 'idx_typmod_v_part');
CALL typmod_idx_snapshot('varchar(15) -> text', 'idx_typmod_v_expr');
CALL typmod_idx_snapshot('varchar(15) -> text', 'idx_typmod_v_part');
ALTER TABLE typmod_test ALTER COLUMN v TYPE text;
CALL typmod_idx_record('varchar(15) -> text', 'idx_typmod_v_expr');
CALL typmod_idx_record('varchar(15) -> text', 'idx_typmod_v_part');
CALL typmod_idx_snapshot('numeric(10, 2) -> numeric(12, 2)', 'idx_typmod_n_expr');
CALL typmod_idx_snapshot('numeric(10, 2) -> numeric(12, 2)', 'idx_typmod_n_part');
ALTER TABLE typmod_test ALTER COLUMN n TYPE numeric(12, 2);
CALL typmod_idx_record('numeric(10, 2) -> numeric(12, 2)', 'idx_typmod_n_expr');
CALL typmod_idx_record('numeric(10, 2) -> numeric(12, 2)', 'idx_typmod_n_part');
CALL typmod_idx_snapshot('numeric(12, 2) -> numeric(14, 4)', 'idx_typmod_n_expr');
CALL typmod_idx_snapshot('numeric(12, 2) -> numeric(14, 4)', 'idx_typmod_n_part');
ALTER TABLE typmod_test ALTER COLUMN n TYPE numeric(14, 4);
CALL typmod_idx_record('numeric(12, 2) -> numeric(14, 4)', 'idx_typmod_n_expr');
CALL typmod_idx_record('numeric(12, 2) -> numeric(14, 4)', 'idx_typmod_n_part');
CALL typmod_idx_snapshot('char(5) -> char(10)', 'idx_typmod_ch_expr');
CALL typmod_idx_snapshot('char(5) -> char(10)', 'idx_typmod_ch_part');
ALTER TABLE typmod_test ALTER COLUMN ch TYPE char(10);
CALL typmod_idx_record('char(5) -> char(10)', 'idx_typmod_ch_expr');
CALL typmod_idx_record('char(5) -> char(10)', 'idx_typmod_ch_part');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT * FROM typmod_test ORDER BY k;
TRUNCATE typmod_idx_info;
DROP TABLE typmod_test;

-- Other types. idx_typmod_witness is on a column that is not altered.
CREATE TABLE typmod_test2 (k int PRIMARY KEY, ts timestamp(0), tstz timestamptz(0), t time(0), tz timetz(0),
    iv interval day, vb varbit(8), vu varchar(10), b bit(8), arr varchar(10)[]);
CREATE INDEX NONCONCURRENTLY idx_typmod_ts_expr ON typmod_test2 (date_trunc('day', ts));
CREATE INDEX NONCONCURRENTLY idx_typmod_ts_part ON typmod_test2 (k) WHERE ts > '2020-01-01';
CREATE INDEX NONCONCURRENTLY idx_typmod_tstz_expr ON typmod_test2 ((tstz AT TIME ZONE 'UTC'));
CREATE INDEX NONCONCURRENTLY idx_typmod_tstz_part ON typmod_test2 (k) WHERE tstz IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_typmod_t_expr ON typmod_test2 ((t + interval '1 hour'));
CREATE INDEX NONCONCURRENTLY idx_typmod_t_part ON typmod_test2 (k) WHERE t > '00:00';
-- YB does not support timetz or interval index keys.
CREATE INDEX NONCONCURRENTLY idx_typmod_tz_expr ON typmod_test2 (date_part('hour', tz));
CREATE INDEX NONCONCURRENTLY idx_typmod_tz_part ON typmod_test2 (k) WHERE tz IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_typmod_iv_expr ON typmod_test2 (date_part('day', iv));
CREATE INDEX NONCONCURRENTLY idx_typmod_iv_part ON typmod_test2 (k) WHERE iv > interval '0';
CREATE INDEX NONCONCURRENTLY idx_typmod_vb_expr ON typmod_test2 (length(vb));
CREATE INDEX NONCONCURRENTLY idx_typmod_vb_part ON typmod_test2 (k) WHERE length(vb) > 0;
CREATE INDEX NONCONCURRENTLY idx_typmod_vu_expr ON typmod_test2 (lower(vu));
CREATE INDEX NONCONCURRENTLY idx_typmod_vu_part ON typmod_test2 (k) WHERE vu <> '';
CREATE INDEX NONCONCURRENTLY idx_typmod_b_expr ON typmod_test2 (length(b));
CREATE INDEX NONCONCURRENTLY idx_typmod_b_part ON typmod_test2 (k) WHERE b IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_typmod_arr_expr ON typmod_test2 (array_length(arr, 1));
CREATE INDEX NONCONCURRENTLY idx_typmod_arr_part ON typmod_test2 (k) WHERE cardinality(arr) > 0;
CREATE INDEX NONCONCURRENTLY idx_typmod_witness ON typmod_test2 (k);
-- b is NULL: widening a non-NULL bit(n) value fails.
INSERT INTO typmod_test2 VALUES (1, '2024-01-02 03:04:05', '2024-01-02 03:04:05+00', '03:04:05', '03:04:05+00',
    '2 days', B'101', 'Abc', NULL, ARRAY['x', 'y']);
CALL typmod_idx_snapshot('timestamp(0) -> timestamp(3)', 'idx_typmod_ts_expr');
CALL typmod_idx_snapshot('timestamp(0) -> timestamp(3)', 'idx_typmod_ts_part');
CALL typmod_idx_snapshot('timestamp(0) -> timestamp(3)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN ts TYPE timestamp(3);
CALL typmod_idx_record('timestamp(0) -> timestamp(3)', 'idx_typmod_ts_expr');
CALL typmod_idx_record('timestamp(0) -> timestamp(3)', 'idx_typmod_ts_part');
CALL typmod_idx_record('timestamp(0) -> timestamp(3)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('timestamptz(0) -> timestamptz(3)', 'idx_typmod_tstz_expr');
CALL typmod_idx_snapshot('timestamptz(0) -> timestamptz(3)', 'idx_typmod_tstz_part');
CALL typmod_idx_snapshot('timestamptz(0) -> timestamptz(3)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN tstz TYPE timestamptz(3);
CALL typmod_idx_record('timestamptz(0) -> timestamptz(3)', 'idx_typmod_tstz_expr');
CALL typmod_idx_record('timestamptz(0) -> timestamptz(3)', 'idx_typmod_tstz_part');
CALL typmod_idx_record('timestamptz(0) -> timestamptz(3)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('time(0) -> time(3)', 'idx_typmod_t_expr');
CALL typmod_idx_snapshot('time(0) -> time(3)', 'idx_typmod_t_part');
CALL typmod_idx_snapshot('time(0) -> time(3)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN t TYPE time(3);
CALL typmod_idx_record('time(0) -> time(3)', 'idx_typmod_t_expr');
CALL typmod_idx_record('time(0) -> time(3)', 'idx_typmod_t_part');
CALL typmod_idx_record('time(0) -> time(3)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('timetz(0) -> timetz(3)', 'idx_typmod_tz_expr');
CALL typmod_idx_snapshot('timetz(0) -> timetz(3)', 'idx_typmod_tz_part');
CALL typmod_idx_snapshot('timetz(0) -> timetz(3)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN tz TYPE timetz(3);
CALL typmod_idx_record('timetz(0) -> timetz(3)', 'idx_typmod_tz_expr');
CALL typmod_idx_record('timetz(0) -> timetz(3)', 'idx_typmod_tz_part');
CALL typmod_idx_record('timetz(0) -> timetz(3)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('interval day -> interval', 'idx_typmod_iv_expr');
CALL typmod_idx_snapshot('interval day -> interval', 'idx_typmod_iv_part');
CALL typmod_idx_snapshot('interval day -> interval', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN iv TYPE interval;
CALL typmod_idx_record('interval day -> interval', 'idx_typmod_iv_expr');
CALL typmod_idx_record('interval day -> interval', 'idx_typmod_iv_part');
CALL typmod_idx_record('interval day -> interval', 'idx_typmod_witness');
CALL typmod_idx_snapshot('varbit(8) -> varbit(16)', 'idx_typmod_vb_expr');
CALL typmod_idx_snapshot('varbit(8) -> varbit(16)', 'idx_typmod_vb_part');
CALL typmod_idx_snapshot('varbit(8) -> varbit(16)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN vb TYPE varbit(16);
CALL typmod_idx_record('varbit(8) -> varbit(16)', 'idx_typmod_vb_expr');
CALL typmod_idx_record('varbit(8) -> varbit(16)', 'idx_typmod_vb_part');
CALL typmod_idx_record('varbit(8) -> varbit(16)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('varchar(10) -> varchar', 'idx_typmod_vu_expr');
CALL typmod_idx_snapshot('varchar(10) -> varchar', 'idx_typmod_vu_part');
CALL typmod_idx_snapshot('varchar(10) -> varchar', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN vu TYPE varchar;
CALL typmod_idx_record('varchar(10) -> varchar', 'idx_typmod_vu_expr');
CALL typmod_idx_record('varchar(10) -> varchar', 'idx_typmod_vu_part');
CALL typmod_idx_record('varchar(10) -> varchar', 'idx_typmod_witness');
CALL typmod_idx_snapshot('bit(8) -> bit(16)', 'idx_typmod_b_expr');
CALL typmod_idx_snapshot('bit(8) -> bit(16)', 'idx_typmod_b_part');
CALL typmod_idx_snapshot('bit(8) -> bit(16)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN b TYPE bit(16);
CALL typmod_idx_record('bit(8) -> bit(16)', 'idx_typmod_b_expr');
CALL typmod_idx_record('bit(8) -> bit(16)', 'idx_typmod_b_part');
CALL typmod_idx_record('bit(8) -> bit(16)', 'idx_typmod_witness');
CALL typmod_idx_snapshot('varchar(10)[] -> varchar(20)[]', 'idx_typmod_arr_expr');
CALL typmod_idx_snapshot('varchar(10)[] -> varchar(20)[]', 'idx_typmod_arr_part');
CALL typmod_idx_snapshot('varchar(10)[] -> varchar(20)[]', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN arr TYPE varchar(20)[];
CALL typmod_idx_record('varchar(10)[] -> varchar(20)[]', 'idx_typmod_arr_expr');
CALL typmod_idx_record('varchar(10)[] -> varchar(20)[]', 'idx_typmod_arr_part');
CALL typmod_idx_record('varchar(10)[] -> varchar(20)[]', 'idx_typmod_witness');
CALL typmod_idx_snapshot('timestamp(3) -> timestamp(0)', 'idx_typmod_ts_expr');
CALL typmod_idx_snapshot('timestamp(3) -> timestamp(0)', 'idx_typmod_ts_part');
CALL typmod_idx_snapshot('timestamp(3) -> timestamp(0)', 'idx_typmod_witness');
ALTER TABLE typmod_test2 ALTER COLUMN ts TYPE timestamp(0);
CALL typmod_idx_record('timestamp(3) -> timestamp(0)', 'idx_typmod_ts_expr');
CALL typmod_idx_record('timestamp(3) -> timestamp(0)', 'idx_typmod_ts_part');
CALL typmod_idx_record('timestamp(3) -> timestamp(0)', 'idx_typmod_witness');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT * FROM typmod_test2 ORDER BY k;
TRUNCATE typmod_idx_info;
DROP TABLE typmod_test2;

-- Generated columns.
CREATE TABLE gen_test (k int PRIMARY KEY, a varchar(10), g varchar(20) GENERATED ALWAYS AS (a || 'g') STORED);
CREATE INDEX NONCONCURRENTLY idx_gen_expr_a ON gen_test (lower(a));
CREATE INDEX NONCONCURRENTLY idx_gen_part_a ON gen_test (k) WHERE a <> '';
CREATE INDEX NONCONCURRENTLY idx_gen_expr_g ON gen_test (lower(g));
CREATE INDEX NONCONCURRENTLY idx_gen_part_g ON gen_test (k) WHERE g <> '';
CREATE INDEX NONCONCURRENTLY idx_gen_expr_a_g ON gen_test (lower(a), g);
INSERT INTO gen_test (k, a) VALUES (1, 'Abc');
ALTER TABLE gen_test ALTER COLUMN a TYPE varchar(20);
CALL typmod_idx_snapshot('alter g', 'idx_gen_expr_a');
CALL typmod_idx_snapshot('alter g', 'idx_gen_part_a');
CALL typmod_idx_snapshot('alter g', 'idx_gen_expr_g');
CALL typmod_idx_snapshot('alter g', 'idx_gen_part_g');
CALL typmod_idx_snapshot('alter g', 'idx_gen_expr_a_g');
ALTER TABLE gen_test ALTER COLUMN g TYPE varchar(30);
CALL typmod_idx_record('alter g', 'idx_gen_expr_a');
CALL typmod_idx_record('alter g', 'idx_gen_part_a');
CALL typmod_idx_record('alter g', 'idx_gen_expr_g');
CALL typmod_idx_record('alter g', 'idx_gen_part_g');
CALL typmod_idx_record('alter g', 'idx_gen_expr_a_g');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT k, g FROM gen_test WHERE lower(g) = 'abcg';
TRUNCATE typmod_idx_info;
DROP TABLE gen_test;

-- User-defined operator classes.
CREATE OPERATOR CLASS udo_text_ops FOR TYPE text USING lsm AS
    OPERATOR 1 <, OPERATOR 2 <=, OPERATOR 3 =, OPERATOR 4 >=, OPERATOR 5 >, FUNCTION 1 bttextcmp(text, text);
CREATE TABLE opclass_test (a varchar(10));
CREATE INDEX NONCONCURRENTLY idx_opclass ON opclass_test (a udo_text_ops);
CREATE INDEX NONCONCURRENTLY idx_expr_opclass ON opclass_test (lower(a) udo_text_ops);
CALL typmod_idx_snapshot('alter a', 'idx_opclass');
CALL typmod_idx_snapshot('alter a', 'idx_expr_opclass');
ALTER TABLE opclass_test ALTER COLUMN a TYPE varchar(30);
CALL typmod_idx_record('alter a', 'idx_opclass');
CALL typmod_idx_record('alter a', 'idx_expr_opclass');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
DROP TABLE opclass_test;

-- GUC yb_alter_type_reuse_expr_and_partial_indexes off.
SHOW yb_alter_type_reuse_expr_and_partial_indexes;
CREATE TABLE guc_test (c varchar(10));
CREATE INDEX NONCONCURRENTLY idx_guc_expr ON guc_test (lower(c));
CREATE INDEX NONCONCURRENTLY idx_guc_partial ON guc_test (c) WHERE c <> '';
CREATE INDEX NONCONCURRENTLY idx_guc_plain ON guc_test (c);
INSERT INTO guc_test VALUES ('Abc');
CALL typmod_idx_snapshot('alter c, reuse off', 'idx_guc_expr');
CALL typmod_idx_snapshot('alter c, reuse off', 'idx_guc_partial');
CALL typmod_idx_snapshot('alter c, reuse off', 'idx_guc_plain');
SET yb_alter_type_reuse_expr_and_partial_indexes = off;
ALTER TABLE guc_test ALTER COLUMN c TYPE varchar(20);
SET yb_alter_type_reuse_expr_and_partial_indexes = on;
CALL typmod_idx_record('alter c, reuse off', 'idx_guc_expr');
CALL typmod_idx_record('alter c, reuse off', 'idx_guc_partial');
CALL typmod_idx_record('alter c, reuse off', 'idx_guc_plain');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT c FROM guc_test WHERE lower(c) = 'abc';
TRUNCATE typmod_idx_info;
DROP TABLE guc_test;

-- Only indexes that reference the altered column, including through INCLUDE, are checked.
-- Reuse is turned off to show which.
-- Indexes with whole-row references cannot be built yet (#35808).
CREATE TABLE dep_test (k int PRIMARY KEY, a varchar(10), b varchar(10));
CREATE INDEX NONCONCURRENTLY idx_dep_expr_a ON dep_test (lower(a));
CREATE INDEX NONCONCURRENTLY idx_dep_part_a ON dep_test (k) WHERE a <> '';
CREATE INDEX NONCONCURRENTLY idx_dep_expr_incl_b ON dep_test (lower(a)) INCLUDE (b);
CREATE INDEX NONCONCURRENTLY idx_dep_part_incl_b ON dep_test (k) INCLUDE (b) WHERE a <> '';
-- expressions are not allowed in INCLUDE.
CREATE INDEX NONCONCURRENTLY idx_dep_incl_expr ON dep_test (k) INCLUDE (lower(b));
INSERT INTO dep_test VALUES (1, 'Abc', 'Def');
CALL typmod_idx_snapshot('alter b, reuse off', 'idx_dep_expr_a');
CALL typmod_idx_snapshot('alter b, reuse off', 'idx_dep_part_a');
CALL typmod_idx_snapshot('alter b, reuse off', 'idx_dep_expr_incl_b');
CALL typmod_idx_snapshot('alter b, reuse off', 'idx_dep_part_incl_b');
SET yb_alter_type_reuse_expr_and_partial_indexes = off;
ALTER TABLE dep_test ALTER COLUMN b TYPE varchar(20);
SET yb_alter_type_reuse_expr_and_partial_indexes = on;
CALL typmod_idx_record('alter b, reuse off', 'idx_dep_expr_a');
CALL typmod_idx_record('alter b, reuse off', 'idx_dep_part_a');
CALL typmod_idx_record('alter b, reuse off', 'idx_dep_expr_incl_b');
CALL typmod_idx_record('alter b, reuse off', 'idx_dep_part_incl_b');
CALL typmod_idx_snapshot('alter b, reuse on', 'idx_dep_expr_a');
CALL typmod_idx_snapshot('alter b, reuse on', 'idx_dep_part_a');
CALL typmod_idx_snapshot('alter b, reuse on', 'idx_dep_expr_incl_b');
CALL typmod_idx_snapshot('alter b, reuse on', 'idx_dep_part_incl_b');
ALTER TABLE dep_test ALTER COLUMN b TYPE varchar(30);
CALL typmod_idx_record('alter b, reuse on', 'idx_dep_expr_a');
CALL typmod_idx_record('alter b, reuse on', 'idx_dep_part_a');
CALL typmod_idx_record('alter b, reuse on', 'idx_dep_expr_incl_b');
CALL typmod_idx_record('alter b, reuse on', 'idx_dep_part_incl_b');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT k, b FROM dep_test WHERE lower(a) = 'abc';
TRUNCATE typmod_idx_info;
DROP TABLE dep_test;

-- Multiple columns altered by one ALTER.
CREATE TABLE multi_test (k int PRIMARY KEY, a varchar(10), b varchar(10));
CREATE INDEX NONCONCURRENTLY idx_multi_expr ON multi_test (lower(a || b));
CREATE INDEX NONCONCURRENTLY idx_multi_part ON multi_test (k) WHERE a <> b;
INSERT INTO multi_test VALUES (1, 'Abc', 'Def');
CALL typmod_idx_snapshot('alter a, b', 'idx_multi_expr');
CALL typmod_idx_snapshot('alter a, b', 'idx_multi_part');
ALTER TABLE multi_test ALTER COLUMN a TYPE varchar(20), ALTER COLUMN b TYPE varchar(20);
CALL typmod_idx_record('alter a, b', 'idx_multi_expr');
CALL typmod_idx_record('alter a, b', 'idx_multi_part');
CALL typmod_idx_snapshot('alter a, b to text', 'idx_multi_expr');
CALL typmod_idx_snapshot('alter a, b to text', 'idx_multi_part');
ALTER TABLE multi_test ALTER COLUMN a TYPE varchar(30), ALTER COLUMN b TYPE text;
CALL typmod_idx_record('alter a, b to text', 'idx_multi_expr');
CALL typmod_idx_record('alter a, b to text', 'idx_multi_part');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT k FROM multi_test WHERE lower(a || b) = 'abcdef';
TRUNCATE typmod_idx_info;
DROP TABLE multi_test;

-- Primary key columns used by expression and partial indexes.
CREATE TABLE pk_test (a varchar(10), b varchar(10), PRIMARY KEY (a, b));
CREATE INDEX NONCONCURRENTLY idx_pk_expr ON pk_test (lower(a || b));
CREATE INDEX NONCONCURRENTLY idx_pk_part ON pk_test (a) WHERE b <> '';
INSERT INTO pk_test VALUES ('Abc', 'Def');
CALL typmod_idx_snapshot('alter a, b', 'pk_test_pkey');
CALL typmod_idx_snapshot('alter a, b', 'idx_pk_expr');
CALL typmod_idx_snapshot('alter a, b', 'idx_pk_part');
ALTER TABLE pk_test ALTER COLUMN a TYPE varchar(20), ALTER COLUMN b TYPE varchar(20);
CALL typmod_idx_record('alter a, b', 'pk_test_pkey');
CALL typmod_idx_record('alter a, b', 'idx_pk_expr');
CALL typmod_idx_record('alter a, b', 'idx_pk_part');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
SELECT a, b FROM pk_test WHERE lower(a || b) = 'abcdef';
TRUNCATE typmod_idx_info;
DROP TABLE pk_test;

--
-- Tests to validate various types of expressions.
--
CREATE FUNCTION expr_udf(text) RETURNS text IMMUTABLE LANGUAGE sql AS 'SELECT $1';
CREATE FUNCTION expr_udf_ne(text, text) RETURNS bool IMMUTABLE LANGUAGE sql AS 'SELECT $1 <> $2';
CREATE OPERATOR <<>> (LEFTARG = text, RIGHTARG = text, FUNCTION = expr_udf_ne);
CREATE TABLE expr_test (k int PRIMARY KEY, c varchar(10), d varchar(20), e varchar(10), f varchar(10));
CREATE INDEX NONCONCURRENTLY idx_expr_lower ON expr_test (lower(c));
CREATE INDEX NONCONCURRENTLY idx_expr_concat ON expr_test ((c || 'x'));
CREATE INDEX NONCONCURRENTLY idx_expr_coalesce ON expr_test (COALESCE(c, d));
CREATE INDEX NONCONCURRENTLY idx_partial_self ON expr_test (c) WHERE c <> '';
CREATE INDEX NONCONCURRENTLY idx_partial_other ON expr_test (k) WHERE c IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_expr_cast ON expr_test ((c::varchar(20)));
CREATE INDEX NONCONCURRENTLY idx_expr_narrow_cast ON expr_test ((c::varchar(15)));
CREATE INDEX NONCONCURRENTLY idx_expr_udf ON expr_test (expr_udf(c));
CREATE INDEX NONCONCURRENTLY idx_partial_udo ON expr_test (k) WHERE c::text <<>> '';
CREATE INDEX NONCONCURRENTLY idx_expr_e_text ON expr_test (lower(e));
CREATE INDEX NONCONCURRENTLY idx_expr_f_collate ON expr_test (lower(f));
CREATE INDEX NONCONCURRENTLY idx_expr_case ON expr_test ((CASE c WHEN 'Abc' THEN 1 ELSE 0 END));
INSERT INTO expr_test VALUES (1, 'Abc', 'zz', 'Ee', 'Ff'), (2, NULL, 'Yy', 'Gg', 'Hh');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_lower');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_concat');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_coalesce');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_partial_self');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_partial_other');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_cast');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_narrow_cast');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_udf');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_partial_udo');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_e_text');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_f_collate');
CALL typmod_idx_snapshot('alter c, e, f', 'idx_expr_case');
ALTER TABLE expr_test ALTER COLUMN c TYPE varchar(20), ALTER COLUMN e TYPE text,
    ALTER COLUMN f TYPE varchar(20) COLLATE "C";
CALL typmod_idx_record('alter c, e, f', 'idx_expr_lower');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_concat');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_coalesce');
CALL typmod_idx_record('alter c, e, f', 'idx_partial_self');
CALL typmod_idx_record('alter c, e, f', 'idx_partial_other');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_cast');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_narrow_cast');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_udf');
CALL typmod_idx_record('alter c, e, f', 'idx_partial_udo');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_e_text');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_f_collate');
CALL typmod_idx_record('alter c, e, f', 'idx_expr_case');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
-- stored trees of reused indexes have the typmod of varchar(20) (which is 24), not varchar(10) (which is 14).
SELECT c.relname, trees LIKE '%:vartypmod 14 %' AS has_old_typmod, trees LIKE '%:vartypmod 24 %' AS has_new_typmod
FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid,
    LATERAL (SELECT COALESCE(i.indexprs::text, '') || ' ' || COALESCE(i.indpred::text, '') AS trees) t
WHERE c.relname IN ('idx_expr_lower', 'idx_expr_concat', 'idx_expr_coalesce', 'idx_expr_narrow_cast',
                    'idx_partial_self', 'idx_partial_other')
ORDER BY c.relname;
-- typmod of an expression index column.
SELECT atttypmod FROM pg_attribute WHERE attrelid = 'idx_expr_coalesce'::regclass AND attnum = 1;
-- the planner still matches the reused indexes.
EXPLAIN (COSTS OFF) /*+ IndexScan(expr_test idx_expr_lower) */ SELECT k FROM expr_test WHERE lower(c) = 'abc';
EXPLAIN (COSTS OFF) /*+ IndexScan(expr_test idx_expr_coalesce) */ SELECT k FROM expr_test WHERE COALESCE(c, d) = 'Abc';
EXPLAIN (COSTS OFF) /*+ IndexScan(expr_test idx_partial_self) */ SELECT c FROM expr_test WHERE c <> '' AND c = 'Abc';
EXPLAIN (COSTS OFF) /*+ IndexScan(expr_test idx_partial_other) */ SELECT k FROM expr_test WHERE c IS NOT NULL;
SELECT k FROM expr_test WHERE lower(c) = 'abc';
SELECT k FROM expr_test WHERE COALESCE(c, d) = 'Yy';
SELECT c FROM expr_test WHERE c <> '' AND c = 'Abc';
-- index maintenance after reuse.
INSERT INTO expr_test VALUES (3, 'LongerThanTen', 'q', 'r', 's');
SELECT k FROM expr_test WHERE lower(c) = 'longerthanten';
SELECT k FROM expr_test WHERE c <> '' AND c = 'LongerThanTen';
-- the cast truncates values longer than 15.
INSERT INTO expr_test VALUES (4, 'abcdefghijklmnopq', 'q', 'r', 's');
EXPLAIN (COSTS OFF) /*+ IndexScan(expr_test idx_expr_narrow_cast) */
    SELECT k FROM expr_test WHERE c::varchar(15) = 'abcdefghijklmno';
/*+ IndexScan(expr_test idx_expr_narrow_cast) */ SELECT k FROM expr_test WHERE c::varchar(15) = 'abcdefghijklmno';
/*+ SeqScan(expr_test) */ SELECT k FROM expr_test WHERE c::varchar(15) = 'abcdefghijklmno';
-- narrowing.
DELETE FROM expr_test WHERE k IN (3, 4);
CALL typmod_idx_snapshot('narrow c', 'idx_expr_lower');
CALL typmod_idx_snapshot('narrow c', 'idx_partial_self');
ALTER TABLE expr_test ALTER COLUMN c TYPE varchar(15);
CALL typmod_idx_record('narrow c', 'idx_expr_lower');
CALL typmod_idx_record('narrow c', 'idx_partial_self');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
DROP TABLE expr_test;

-- Various expression types.
CREATE DOMAIN pin_dom AS text;
CREATE TABLE pin_test (k int PRIMARY KEY, c varchar(10));
-- Supported.
CREATE INDEX NONCONCURRENTLY idx_pin_ok_funcexpr ON pin_test (lower(c));
CREATE INDEX NONCONCURRENTLY idx_pin_ok_opexpr ON pin_test ((c || 'x'));
CREATE INDEX NONCONCURRENTLY idx_pin_ok_distinctexpr ON pin_test (k) WHERE c IS DISTINCT FROM 'a';
CREATE INDEX NONCONCURRENTLY idx_pin_ok_nullifexpr ON pin_test ((NULLIF(c, '')));
-- c IN (...) is not reused, so use ANY.
CREATE INDEX NONCONCURRENTLY idx_pin_ok_scalararrayopexpr ON pin_test (k) WHERE c::text = ANY (ARRAY['a', 'b']);
CREATE INDEX NONCONCURRENTLY idx_pin_ok_boolexpr ON pin_test (k) WHERE c = 'a' OR c = 'b';
CREATE INDEX NONCONCURRENTLY idx_pin_ok_caseexpr ON pin_test ((CASE WHEN c = 'a' THEN 1 ELSE 0 END));
CREATE INDEX NONCONCURRENTLY idx_pin_ok_coalesceexpr ON pin_test ((COALESCE(c, 'x')));
CREATE INDEX NONCONCURRENTLY idx_pin_ok_minmaxexpr ON pin_test ((GREATEST(c, 'm')));
CREATE INDEX NONCONCURRENTLY idx_pin_ok_nulltest ON pin_test (k) WHERE c IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_pin_ok_booleantest ON pin_test (k) WHERE (c = 'a') IS NOT TRUE;
CREATE INDEX NONCONCURRENTLY idx_pin_ok_arrayexpr ON pin_test (k) WHERE ARRAY[c] <> '{}';
CREATE INDEX NONCONCURRENTLY idx_pin_ok_rowexpr ON pin_test (k) WHERE ROW(c, k) IS NOT NULL;
CREATE INDEX NONCONCURRENTLY idx_pin_ok_collateexpr ON pin_test (lower(c COLLATE "C"));
-- Not supported.
CREATE INDEX NONCONCURRENTLY idx_pin_no_coerceviaio ON pin_test (k) WHERE length(c) > 0 AND k::text <> '';
CREATE INDEX NONCONCURRENTLY idx_pin_no_subscriptingref ON pin_test (k) WHERE (ARRAY[c])[1] <> '';
-- a cast on an ARRAY[...] literal casts its elements, so cast an expression.
CREATE INDEX NONCONCURRENTLY idx_pin_no_arraycoerceexpr ON pin_test (k) WHERE (ARRAY[c] || ARRAY[c])::text[] <> '{}';
CREATE INDEX NONCONCURRENTLY idx_pin_no_coercetodomain ON pin_test ((c::pin_dom));
INSERT INTO pin_test VALUES (1, 'a'), (2, 'Abc');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_funcexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_opexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_distinctexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_nullifexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_scalararrayopexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_boolexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_caseexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_coalesceexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_minmaxexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_nulltest');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_booleantest');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_arrayexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_rowexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_ok_collateexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_no_coerceviaio');
CALL typmod_idx_snapshot('alter c', 'idx_pin_no_subscriptingref');
CALL typmod_idx_snapshot('alter c', 'idx_pin_no_arraycoerceexpr');
CALL typmod_idx_snapshot('alter c', 'idx_pin_no_coercetodomain');
ALTER TABLE pin_test ALTER COLUMN c TYPE varchar(20);
CALL typmod_idx_record('alter c', 'idx_pin_ok_funcexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_opexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_distinctexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_nullifexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_scalararrayopexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_boolexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_caseexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_coalesceexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_minmaxexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_nulltest');
CALL typmod_idx_record('alter c', 'idx_pin_ok_booleantest');
CALL typmod_idx_record('alter c', 'idx_pin_ok_arrayexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_rowexpr');
CALL typmod_idx_record('alter c', 'idx_pin_ok_collateexpr');
CALL typmod_idx_record('alter c', 'idx_pin_no_coerceviaio');
CALL typmod_idx_record('alter c', 'idx_pin_no_subscriptingref');
CALL typmod_idx_record('alter c', 'idx_pin_no_arraycoerceexpr');
CALL typmod_idx_record('alter c', 'idx_pin_no_coercetodomain');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
DROP TABLE pin_test;

-- An index column collation that differs from the expression's.
-- COLLATE "en_US" is repeated so that only the typmod changes.
CREATE TABLE coll_test (k int PRIMARY KEY, c varchar(10) COLLATE "en_US", d varchar(10));
CREATE INDEX NONCONCURRENTLY idx_coll_expr ON coll_test ((lower(c) COLLATE "C"));
CREATE INDEX NONCONCURRENTLY idx_coll_include ON coll_test (lower(c)) INCLUDE (d);
INSERT INTO coll_test VALUES (1, 'b', 'x'), (2, 'B', 'y'), (3, 'a', 'z');
CALL typmod_idx_snapshot('alter c, d', 'idx_coll_expr');
CALL typmod_idx_snapshot('alter c, d', 'idx_coll_include');
ALTER TABLE coll_test ALTER COLUMN c TYPE varchar(20) COLLATE "en_US", ALTER COLUMN d TYPE varchar(20);
CALL typmod_idx_record('alter c, d', 'idx_coll_expr');
CALL typmod_idx_record('alter c, d', 'idx_coll_include');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
INSERT INTO coll_test VALUES (4, 'c', 'w'), (5, 'C', 'v');
/*+ IndexScan(coll_test idx_coll_expr) */ SELECT k FROM coll_test WHERE (lower(c) COLLATE "C") IN ('b', 'c') ORDER BY k;
/*+ IndexOnlyScan(coll_test idx_coll_include) */ SELECT d FROM coll_test WHERE lower(c) = 'a';
DROP TABLE coll_test;

-- An operator class whose key type differs from the expression's type.
CREATE TABLE gin_test (k int PRIMARY KEY, c varchar(10));
CREATE INDEX NONCONCURRENTLY idx_gin_expr ON gin_test USING ybgin (to_tsvector('simple', c));
INSERT INTO gin_test VALUES (1, 'abc'), (2, 'xyz');
CALL typmod_idx_snapshot('alter c', 'idx_gin_expr');
ALTER TABLE gin_test ALTER COLUMN c TYPE varchar(20);
CALL typmod_idx_record('alter c', 'idx_gin_expr');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
SELECT format_type(a.atttypid, a.atttypmod) FROM pg_attribute a WHERE a.attrelid = 'idx_gin_expr'::regclass;
INSERT INTO gin_test VALUES (3, 'abc longer than ten');
/*+ IndexScan(gin_test idx_gin_expr) */
    SELECT k FROM gin_test WHERE to_tsvector('simple', c) @@ to_tsquery('simple', 'abc') ORDER BY k;
DROP TABLE gin_test;

--
-- Partitioned tables, with partition indexes that are cloned from or adopted by the parent's index.
--
-- Cloned by CREATE INDEX on the parent.
CREATE TABLE expr_part (p int, k varchar(10), c varchar(10), PRIMARY KEY (p, k)) PARTITION BY LIST (p);
CREATE TABLE expr_part_1 PARTITION OF expr_part FOR VALUES IN (1);
CREATE INDEX NONCONCURRENTLY idx_expr_part ON expr_part (lower(c));
CREATE INDEX NONCONCURRENTLY idx_partial_part ON expr_part (k) WHERE c <> '';
CALL typmod_idx_snapshot('alter c, k', 'expr_part_pkey');
CALL typmod_idx_snapshot('alter c, k', 'expr_part_1_pkey');
CALL typmod_idx_snapshot('alter c, k', 'idx_expr_part');
CALL typmod_idx_snapshot('alter c, k', 'expr_part_1_lower_idx');
CALL typmod_idx_snapshot('alter c, k', 'idx_partial_part');
CALL typmod_idx_snapshot('alter c, k', 'expr_part_1_k_idx');
ALTER TABLE expr_part ALTER COLUMN c TYPE varchar(20), ALTER COLUMN k TYPE varchar(20);
CALL typmod_idx_record('alter c, k', 'expr_part_pkey');
CALL typmod_idx_record('alter c, k', 'expr_part_1_pkey');
CALL typmod_idx_record('alter c, k', 'idx_expr_part');
CALL typmod_idx_record('alter c, k', 'expr_part_1_lower_idx');
CALL typmod_idx_record('alter c, k', 'idx_partial_part');
CALL typmod_idx_record('alter c, k', 'expr_part_1_k_idx');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
SELECT i.indexrelid::regclass, inh.inhparent::regclass
FROM pg_index i LEFT JOIN pg_inherits inh ON inh.inhrelid = i.indexrelid
WHERE i.indrelid = 'expr_part_1'::regclass ORDER BY 1;
DROP TABLE expr_part;

-- Adopted by CREATE INDEX on the parent, with the partition's columns in a different order.
CREATE TABLE att_parent (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE att_child (c varchar(10), k int NOT NULL, PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY att_child_lower_idx ON att_child (lower(c)) WHERE c <> '';
ALTER TABLE att_parent ATTACH PARTITION att_child FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY att_parent_lower_idx ON att_parent (lower(c)) WHERE c <> '';
INSERT INTO att_parent VALUES (1, 'Abc');
SELECT i.inhrelid::regclass, i.inhparent::regclass FROM pg_inherits i
WHERE i.inhparent = 'att_parent_lower_idx'::regclass;
CALL typmod_idx_snapshot('alter c', 'att_parent_lower_idx');
CALL typmod_idx_snapshot('alter c', 'att_child_lower_idx');
ALTER TABLE att_parent ALTER COLUMN c TYPE varchar(20);
CALL typmod_idx_record('alter c', 'att_parent_lower_idx');
CALL typmod_idx_record('alter c', 'att_child_lower_idx');
SELECT * FROM typmod_idx_results ORDER BY description, relname;
TRUNCATE typmod_idx_info;
SELECT i.inhrelid::regclass, i.inhparent::regclass FROM pg_inherits i
WHERE i.inhparent = 'att_parent_lower_idx'::regclass;
INSERT INTO att_parent VALUES (2, 'Longer than ten');
/*+ IndexScan(att_child att_child_lower_idx) */ SELECT k FROM att_parent WHERE lower(c) = 'longer than ten' AND c <> '';

CREATE TEMP TABLE part_idx_snap (relname name, oid oid, relfilenode oid);
CREATE FUNCTION part_idx_snapshot(root regclass) RETURNS void LANGUAGE sql AS $$
    DELETE FROM part_idx_snap;
    INSERT INTO part_idx_snap SELECT c.relname, c.oid, c.relfilenode
    FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
    WHERE i.indrelid IN (SELECT relid FROM pg_partition_tree(root)) AND NOT i.indisprimary;
$$;
CREATE FUNCTION part_idx_report(root regclass)
    RETURNS TABLE (tbl regclass, idx regclass, oid text, relfilenode text, attached_to regclass, valid bool)
    LANGUAGE sql AS $$
    SELECT i.indrelid::regclass, i.indexrelid::regclass,
        CASE WHEN c.oid = s.oid THEN 'reused' ELSE 'changed' END,
        CASE WHEN c.relfilenode = s.relfilenode THEN 'reused' ELSE 'changed' END,
        inh.inhparent::regclass, i.indisvalid
    FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
        LEFT JOIN part_idx_snap s ON s.relname = c.relname
        LEFT JOIN pg_inherits inh ON inh.inhrelid = i.indexrelid
    WHERE i.indrelid IN (SELECT relid FROM pg_partition_tree(root)) AND NOT i.indisprimary
    ORDER BY i.indrelid::regclass::text, i.indexrelid::regclass::text;
$$;
DROP TABLE att_parent;

-- Cloned by CREATE TABLE ... PARTITION OF.
CREATE TABLE pclone (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY pclone_idx ON pclone (k) WHERE c::text = ANY (ARRAY['a', 'b']);
CREATE TABLE pclone_1 PARTITION OF pclone FOR VALUES FROM (1) TO (100);
SELECT part_idx_snapshot('pclone');
ALTER TABLE pclone ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pclone');
DROP TABLE pclone;

-- Adopted by CREATE INDEX on the parent, with a different but equivalent predicate.
CREATE TABLE pdef (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pdef_1 PARTITION OF pdef FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY pdef_1_idx ON pdef_1 (k) WHERE c IN ('a', 'b');
CREATE INDEX NONCONCURRENTLY pdef_idx ON pdef (k) WHERE c::text = ANY (ARRAY['a', 'b']);
SELECT inhrelid::regclass FROM pg_inherits WHERE inhparent = 'pdef_idx'::regclass;
SELECT part_idx_snapshot('pdef');
ALTER TABLE pdef ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pdef');
DROP TABLE pdef;

-- Adopted by ATTACH PARTITION, with the same predicate.
CREATE TABLE patt_same (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY patt_same_idx ON patt_same (k) WHERE c::text = ANY (ARRAY['a', 'b']);
CREATE TABLE patt_same_1 (k int, c varchar(10), PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY patt_same_1_idx ON patt_same_1 (k) WHERE c::text = ANY (ARRAY['a', 'b']);
ALTER TABLE patt_same ATTACH PARTITION patt_same_1 FOR VALUES FROM (1) TO (100);
SELECT part_idx_snapshot('patt_same');
ALTER TABLE patt_same ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('patt_same');
DROP TABLE patt_same;

-- Adopted by ATTACH PARTITION, with a different but equivalent predicate.
CREATE TABLE patt_diff (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY patt_diff_idx ON patt_diff (k) WHERE c::text = ANY (ARRAY['a', 'b']);
CREATE TABLE patt_diff_1 (k int, c varchar(10), PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY patt_diff_1_idx ON patt_diff_1 (k) WHERE c IN ('a', 'b');
ALTER TABLE patt_diff ATTACH PARTITION patt_diff_1 FOR VALUES FROM (1) TO (100);
INSERT INTO patt_diff VALUES (1, 'a'), (2, 'z');
SELECT inhrelid::regclass FROM pg_inherits WHERE inhparent = 'patt_diff_idx'::regclass;
SELECT part_idx_snapshot('patt_diff');
ALTER TABLE patt_diff ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('patt_diff');
/*+ IndexScan(patt_diff_1) */ SELECT k FROM patt_diff WHERE c IN ('a', 'b');
DROP TABLE patt_diff;

-- Adopted by ALTER INDEX ... ATTACH PARTITION, with the same predicate and with a different but equivalent one.
CREATE TABLE pidx_same (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pidx_same_1 PARTITION OF pidx_same FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY pidx_same_idx ON ONLY pidx_same (k) WHERE c <> '' AND k > 0;
CREATE INDEX NONCONCURRENTLY pidx_same_1_idx ON pidx_same_1 (k) WHERE c <> '' AND k > 0;
ALTER INDEX pidx_same_idx ATTACH PARTITION pidx_same_1_idx;
SELECT part_idx_snapshot('pidx_same');
ALTER TABLE pidx_same ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pidx_same');
DROP TABLE pidx_same;
CREATE TABLE pidx_diff (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pidx_diff_1 PARTITION OF pidx_diff FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY pidx_diff_idx ON ONLY pidx_diff (k) WHERE c <> '' AND k > 0;
CREATE INDEX NONCONCURRENTLY pidx_diff_1_idx ON pidx_diff_1 (k) WHERE c <> '' AND k > '0'::text::int;
ALTER INDEX pidx_diff_idx ATTACH PARTITION pidx_diff_1_idx;
SELECT part_idx_snapshot('pidx_diff');
ALTER TABLE pidx_diff ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pidx_diff');
DROP TABLE pidx_diff;

-- Adopted by ATTACH PARTITION, with a cast that is a no-op only before the ALTER.
CREATE TABLE pcast (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY pcast_idx ON pcast ((c::text));
CREATE TABLE pcast_1 (k int, c varchar(10), PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY pcast_1_idx ON pcast_1 (((c::varchar(15))::text));
ALTER TABLE pcast ATTACH PARTITION pcast_1 FOR VALUES FROM (1) TO (100);
SELECT inhrelid::regclass FROM pg_inherits WHERE inhparent = 'pcast_idx'::regclass;
SELECT part_idx_snapshot('pcast');
ALTER TABLE pcast ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pcast');
SELECT pg_get_indexdef(i.indexrelid) FROM pg_index i WHERE i.indrelid = 'pcast_1'::regclass AND NOT i.indisprimary;
DROP TABLE pcast;

-- Two levels, with a different but equivalent predicate on the leaf.
CREATE TABLE pmulti (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pmulti_1 PARTITION OF pmulti FOR VALUES FROM (1) TO (100) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY pmulti_idx ON pmulti (k) WHERE c::text = ANY (ARRAY['a', 'b']);
CREATE TABLE pmulti_1_1 (k int, c varchar(10), PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY pmulti_1_1_idx ON pmulti_1_1 (k) WHERE c IN ('a', 'b');
ALTER TABLE pmulti_1 ATTACH PARTITION pmulti_1_1 FOR VALUES FROM (1) TO (50);
SELECT part_idx_snapshot('pmulti');
ALTER TABLE pmulti ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pmulti');
DROP TABLE pmulti;

-- Partitions with columns in a different order from the root's.
-- Adopted by ATTACH PARTITION.
CREATE TABLE pord_att (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY pord_att_idx ON pord_att (lower(c)) WHERE c <> '';
CREATE TABLE pord_att_1 (c varchar(10), k int NOT NULL, PRIMARY KEY (k));
CREATE INDEX NONCONCURRENTLY pord_att_1_idx ON pord_att_1 (lower(c)) WHERE c <> '';
ALTER TABLE pord_att ATTACH PARTITION pord_att_1 FOR VALUES FROM (1) TO (100);
SELECT part_idx_snapshot('pord_att');
ALTER TABLE pord_att ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pord_att');
DROP TABLE pord_att;

-- Cloned by ATTACH PARTITION.
CREATE TABLE pord_clone (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE INDEX NONCONCURRENTLY pord_clone_idx ON pord_clone (lower(c)) WHERE c <> '';
CREATE TABLE pord_clone_1 (c varchar(10), k int NOT NULL, PRIMARY KEY (k));
ALTER TABLE pord_clone ATTACH PARTITION pord_clone_1 FOR VALUES FROM (1) TO (100);
SELECT part_idx_snapshot('pord_clone');
ALTER TABLE pord_clone ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pord_clone');
DROP TABLE pord_clone;

-- Adopted by ALTER INDEX ... ATTACH PARTITION.
CREATE TABLE pord_idx (k int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pord_idx_1 (c varchar(10), k int NOT NULL, PRIMARY KEY (k));
ALTER TABLE pord_idx ATTACH PARTITION pord_idx_1 FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY pord_idx_idx ON ONLY pord_idx (lower(c)) WHERE c <> '';
CREATE INDEX NONCONCURRENTLY pord_idx_1_idx ON pord_idx_1 (lower(c)) WHERE c <> '';
ALTER INDEX pord_idx_idx ATTACH PARTITION pord_idx_1_idx;
SELECT part_idx_snapshot('pord_idx');
ALTER TABLE pord_idx ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pord_idx');
DROP TABLE pord_idx;

-- Cloned by CREATE TABLE ... PARTITION OF, after a column is dropped from the root.
CREATE TABLE pord_drop (k int, x int, c varchar(10), PRIMARY KEY (k)) PARTITION BY RANGE (k);
ALTER TABLE pord_drop DROP COLUMN x;
CREATE TABLE pord_drop_1 PARTITION OF pord_drop FOR VALUES FROM (1) TO (100);
CREATE INDEX NONCONCURRENTLY pord_drop_idx ON pord_drop (lower(c)) WHERE c <> '';
SELECT attrelid::regclass, attnum FROM pg_attribute
WHERE attrelid IN ('pord_drop'::regclass, 'pord_drop_1'::regclass) AND attname = 'c' ORDER BY 1;
INSERT INTO pord_drop VALUES (1, 'Abc');
SELECT part_idx_snapshot('pord_drop');
ALTER TABLE pord_drop ALTER COLUMN c TYPE varchar(20);
SELECT * FROM part_idx_report('pord_drop');
/*+ IndexScan(pord_drop_1) */ SELECT k FROM pord_drop WHERE lower(c) = 'abc' AND c <> '';
DROP TABLE pord_drop;

-- Reused indexes keep their own collation, which differs from the column's.
CREATE TABLE idx_coll (k int PRIMARY KEY, c varchar(10) COLLATE "en_US", d varchar(10) COLLATE "en_US");
CREATE INDEX NONCONCURRENTLY idx_coll_c_idx ON idx_coll (c COLLATE "C" ASC) INCLUDE (d);
CREATE INDEX NONCONCURRENTLY idx_coll_expr_idx ON idx_coll ((substr(c, 1, 5)) COLLATE "C" ASC);
CREATE INDEX NONCONCURRENTLY idx_coll_part_idx ON idx_coll (c COLLATE "C" ASC) WHERE c <> 'z';
INSERT INTO idx_coll VALUES (1, 'b', 'x'), (2, 'B', 'x'), (3, 'a', 'x');
CREATE TEMP TABLE idx_coll_info AS
    SELECT relname, oid, relfilenode FROM pg_class WHERE relname LIKE 'idx_coll_%_idx';
ALTER TABLE idx_coll ALTER COLUMN c TYPE varchar(20) COLLATE "en_US",
    ALTER COLUMN d TYPE varchar(20) COLLATE "en_US";
SELECT old.relname,
    CASE WHEN old.relfilenode = new.relfilenode THEN 'relfilenode reused' ELSE 'relfilenode changed' END,
    CASE WHEN old.oid = new.oid THEN 'OID reused' ELSE 'OID changed' END
FROM idx_coll_info old JOIN pg_class new ON new.relname = old.relname ORDER BY 1;
SELECT attrelid::regclass, attname, atttypmod, attcollation::regcollation FROM pg_attribute
WHERE attrelid IN (SELECT oid FROM idx_coll_info) ORDER BY 1, attnum;
INSERT INTO idx_coll VALUES (4, 'c', 'x'), (5, 'C', 'x');
-- The column's collation orders the rows differently from "C".
SELECT c FROM idx_coll ORDER BY c COLLATE "en_US", k;
EXPLAIN (COSTS OFF) /*+ IndexOnlyScan(idx_coll idx_coll_c_idx) */
SELECT c FROM idx_coll ORDER BY c COLLATE "C";
/*+ IndexOnlyScan(idx_coll idx_coll_c_idx) */ SELECT c FROM idx_coll ORDER BY c COLLATE "C";
EXPLAIN (COSTS OFF) /*+ IndexScan(idx_coll idx_coll_c_idx) */
SELECT k FROM idx_coll WHERE c COLLATE "C" IN ('b', 'c') ORDER BY k;
/*+ IndexScan(idx_coll idx_coll_c_idx) */ SELECT k FROM idx_coll WHERE c COLLATE "C" IN ('b', 'c') ORDER BY k;
EXPLAIN (COSTS OFF) /*+ IndexScan(idx_coll idx_coll_expr_idx) */
SELECT k FROM idx_coll ORDER BY substr(c, 1, 5) COLLATE "C";
/*+ IndexScan(idx_coll idx_coll_expr_idx) */ SELECT k FROM idx_coll ORDER BY substr(c, 1, 5) COLLATE "C";
/*+ IndexScan(idx_coll idx_coll_expr_idx) */
SELECT k FROM idx_coll WHERE substr(c, 1, 5) COLLATE "C" IN ('b', 'c') ORDER BY k;
EXPLAIN (COSTS OFF) /*+ IndexOnlyScan(idx_coll idx_coll_part_idx) */
SELECT c FROM idx_coll WHERE c <> 'z' ORDER BY c COLLATE "C";
/*+ IndexOnlyScan(idx_coll idx_coll_part_idx) */
SELECT c FROM idx_coll WHERE c <> 'z' ORDER BY c COLLATE "C";
/*+ IndexOnlyScan(idx_coll idx_coll_part_idx) */
SELECT c FROM idx_coll WHERE c COLLATE "C" IN ('b', 'c') AND c <> 'z' ORDER BY 1;
-- No stale index entries after a delete.
DELETE FROM idx_coll WHERE k = 1;
/*+ IndexOnlyScan(idx_coll idx_coll_c_idx) */ SELECT c FROM idx_coll WHERE c COLLATE "C" = 'b';
/*+ IndexScan(idx_coll idx_coll_expr_idx) */ SELECT k FROM idx_coll WHERE substr(c, 1, 5) COLLATE "C" = 'b';
/*+ IndexOnlyScan(idx_coll idx_coll_part_idx) */
SELECT c FROM idx_coll WHERE c COLLATE "C" = 'b' AND c <> 'z';
DROP TABLE idx_coll;
RESET yb_alter_type_reuse_expr_and_partial_indexes;
