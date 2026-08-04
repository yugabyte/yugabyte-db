--
-- CREATE_TABLE_AS
-- Taken from Postgres test/regress/sql/select_into.sql
-- Test CTAS and SELECT INTO commands.
--

CREATE TABLE ctas_source_table (
    id int4 PRIMARY KEY,
    name text,
    value int4,
    notes text
);

CREATE INDEX ctas_source_name_idx ON ctas_source_table(name);

INSERT INTO ctas_source_table VALUES
        (1, 'name_1', 100, 'note_a'),
        (2, 'name_2', 200, 'note_b'),
        (3, 'name_3', 300, 'note_c'),
        (4, 'name_4', 400, 'note_d');

-- basic ctas
CREATE TABLE ctas_t1 AS SELECT * FROM ctas_source_table;

-- ctas with filter
CREATE TABLE ctas_t2 AS SELECT name, notes FROM ctas_source_table WHERE id > 2;

-- Test command that results in no rows
CREATE TABLE ctas_t3 AS SELECT id, value * 10 AS value_times_10 FROM ctas_source_table WHERE id > 99;

-- Basic WITH DATA / WITH NO DATA
CREATE TABLE tbl_withdata1 (a)
  AS SELECT generate_series(1,3) WITH DATA;
CREATE TABLE tbl_nodata1 (a) AS
  SELECT generate_series(1,3) WITH NO DATA;

-- Tests for WITH NO DATA and column name consistency
CREATE TABLE ctas_base (i int, j int);
INSERT INTO ctas_base VALUES (1, 2);
CREATE TABLE ctas_nodata (ii, jj) AS SELECT i, j FROM ctas_base;
CREATE TABLE ctas_nodata_2 (ii, jj) AS SELECT i, j FROM ctas_base WITH NO DATA;
CREATE TABLE ctas_nodata_3 (ii) AS SELECT i, j FROM ctas_base;
CREATE TABLE ctas_nodata_4 (ii) AS SELECT i, j FROM ctas_base WITH NO DATA;

-- Test CREATE TABLE AS ... IF NOT EXISTS
CREATE TABLE ctas_ine_tbl AS SELECT 1;
CREATE TABLE IF NOT EXISTS ctas_ine_tbl AS SELECT 1 / 0;
CREATE TABLE IF NOT EXISTS ctas_ine_tbl AS SELECT 1 / 0 WITH NO DATA;
PREPARE ctas_ine_query AS SELECT 1 / 0;

-- Partitioned table tests
CREATE TABLE ctas_part_source (part_id int, part_val text) PARTITION BY RANGE(part_id);
CREATE TABLE ctas_part_source_p1 PARTITION OF ctas_part_source FOR VALUES FROM (0) TO (100);
CREATE TABLE ctas_part_source_p2 PARTITION OF ctas_part_source FOR VALUES FROM (100) TO (200);
INSERT INTO ctas_part_source SELECT i, 'value' || i FROM generate_series(1, 150) i;

-- CTAS from partitioned parent and children
CREATE TABLE ctas_from_parent AS SELECT * FROM ctas_part_source WHERE part_id < 150;
CREATE TABLE ctas_from_child1 AS SELECT * FROM ctas_part_source_p1;
CREATE TABLE ctas_from_child2 AS SELECT * FROM ctas_part_source_p2;

-- SELECT INTO from partitioned parent and children
SELECT * INTO select_into_from_parent FROM ctas_part_source WHERE part_id < 150;
SELECT * INTO select_into_from_child1 FROM ctas_part_source_p1;
SELECT * INTO select_into_from_child2 FROM ctas_part_source_p2;
