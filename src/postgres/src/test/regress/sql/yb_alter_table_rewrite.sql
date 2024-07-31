
--
-- ALTER TABLE with table rewrite tests.
--
-- Suppress NOTICE messages during table rewrite operations.
SET client_min_messages TO WARNING;
-- Test add column operation with table rewrite.
CREATE TABLESPACE test_tablespace LOCATION '/invalid';
CREATE TABLE table_rewrite_test1_fk1(a int PRIMARY KEY);
INSERT INTO table_rewrite_test1_fk1 VALUES (1), (2), (3), (4);
CREATE TABLE table_rewrite_test1
    (a int REFERENCES table_rewrite_test1_fk1(a), b int, c text,
     d int generated always as identity, drop_me int, e int DEFAULT 10,
     f int NOT NULL, username text,
     PRIMARY KEY (a ASC))
    TABLESPACE test_tablespace;
ALTER TABLE table_rewrite_test1 DROP COLUMN drop_me;
INSERT INTO table_rewrite_test1 (a, b, c, e, f, username)
    VALUES(1, 1, 'text', 10, 1, 'user1'), (2, 2, 'text2', 20, 2, 'user2'),
    (3, 3, 'text33', 30, 3, 'user3');
-- create some dependencies on the table.
-- 1. check constraint.
ALTER TABLE table_rewrite_test1 ADD CONSTRAINT a_check CHECK(a > 0);
-- 2. indexes.
CREATE UNIQUE INDEX test_index ON table_rewrite_test1(b)
    TABLESPACE test_tablespace;
CREATE INDEX test_index2 ON table_rewrite_test1((e * 2))
    TABLESPACE test_tablespace;
-- 3. foreign key constraint.
CREATE TABLE table_rewrite_test1_fk2(a int REFERENCES table_rewrite_test1(b));
-- 4. dependent view.
CREATE VIEW table_rewrite_test1_view AS SELECT a, b FROM table_rewrite_test1;
-- 5. column with missing default value.
ALTER TABLE table_rewrite_test1 ADD COLUMN g int DEFAULT 5;
-- 6. dependent trigger.
CREATE FUNCTION dummy() RETURNS TRIGGER
AS $$
BEGIN
    RETURN NULL;
END;
$$ LANGUAGE PLPGSQL;
CREATE TRIGGER dummy_trigger
    AFTER INSERT
    ON table_rewrite_test1
    FOR EACH ROW
    EXECUTE PROCEDURE dummy();
-- 6. enable RLS and create policy objects.
ALTER TABLE table_rewrite_test1 ENABLE ROW LEVEL SECURITY;
CREATE USER user1;
CREATE POLICY p ON table_rewrite_test1
    FOR SELECT TO user1 USING (username = CURRENT_USER);
GRANT SELECT ON table_rewrite_test1 TO user1;
-- 7. extended statistics object.
CREATE STATISTICS s1(dependencies) ON a, b FROM table_rewrite_test1;
-- perform several table rewrite operations.
ALTER TABLE table_rewrite_test1 ADD COLUMN h SERIAL, ADD COLUMN i BIGSERIAL;
ALTER TABLE table_rewrite_test1
    ADD COLUMN j timestamp DEFAULT clock_timestamp() NOT NULL,
    DROP CONSTRAINT table_rewrite_test1_pkey;
ALTER TABLE table_rewrite_test1 ADD PRIMARY KEY(a DESC);
ALTER TABLE table_rewrite_test1 ALTER c TYPE int USING LENGTH(c);
-- verify table data.
SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 ORDER BY a;
-- verify that we preserved dependent objects.
\d table_rewrite_test1;
\d test_index;
\d test_index2;
SELECT * FROM table_rewrite_test1_view ORDER BY a;
EXPLAIN SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 WHERE b = 3;
SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 WHERE b = 3;
EXPLAIN SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 WHERE e * 2 = 20;
SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 WHERE e * 2 = 20;
-- verify PK constraint.
INSERT INTO table_rewrite_test1(a, f) VALUES (3, 4); -- should fail.
-- verify FK constraint where the altered table references a FK table.
INSERT INTO table_rewrite_test1(a, f) VALUES (4, 4);
INSERT INTO table_rewrite_test1(a, f) VALUES (5, 5); -- should fail.
INSERT INTO table_rewrite_test1_fk1 VALUES (5), (6);
INSERT INTO table_rewrite_test1(a, f) VALUES (5, 5);
DELETE FROM table_rewrite_test1_fk1 WHERE a = 3; -- should fail.
-- verify FK constraint where another table references the altered table via
-- an FK constraint..
INSERT INTO table_rewrite_test1_fk2(a) VALUES (3);
INSERT INTO table_rewrite_test1_fk2(a) VALUES (6); -- should fail.
INSERT INTO table_rewrite_test1(a, b, f) VALUES (6, 4, 4);
INSERT INTO table_rewrite_test1_fk2(a) VALUES (4);
DELETE FROM table_rewrite_test1 WHERE b = 3; -- should fail.
DELETE FROM table_rewrite_test1_fk2;
DELETE FROM table_rewrite_test1 WHERE b = 3;
INSERT INTO table_rewrite_test1_fk2 VALUES (3); -- should fail.
-- verify policies and RLS.
SET ROLE user1;
SELECT a, b, username, c, d, e, f, g, h, i FROM table_rewrite_test1 ORDER BY a;
SET ROLE yugabyte;

-- verify future ALTER operations on the table succeed.
ALTER TABLE table_rewrite_test1 ADD COLUMN k int, DROP COLUMN b CASCADE;
ALTER TABLE table_rewrite_test1 RENAME TO table_rewrite_test1_new;

-- cleanup.
DROP TABLE table_rewrite_test1_new CASCADE;
DROP USER user1;

-- Test table rewrite operations on a partitioned table.
CREATE TABLE test_partitioned(a int, PRIMARY KEY (a ASC), b text) PARTITION BY RANGE (a);
CREATE TABLE test_partitioned_part1 PARTITION OF test_partitioned FOR VALUES FROM (1) TO (6);
CREATE TABLE test_partitioned_part2 PARTITION OF test_partitioned FOR VALUES FROM (6) TO (11);
INSERT INTO test_partitioned VALUES(generate_series(1, 10), 'text');
ALTER TABLE test_partitioned ADD COLUMN c SERIAL, ALTER COLUMN b TYPE int USING length(b);
ALTER TABLE test_partitioned DROP CONSTRAINT test_partitioned_pkey;
ALTER TABLE test_partitioned ADD PRIMARY KEY (a ASC);
SELECT * FROM test_partitioned;

-- Test table rewrite on temp tables.
CREATE TEMP TABLE temp_table_rewrite_test(a int);
INSERT INTO temp_table_rewrite_test VALUES(1), (2), (3);
CREATE INDEX ON temp_table_rewrite_test(a);
ALTER TABLE temp_table_rewrite_test ADD COLUMN c SERIAL;
SELECT * FROM temp_table_rewrite_test ORDER BY a;

-- Test rewrite with colocation.
CREATE DATABASE mydb WITH colocation = true;
\c mydb;
-- Suppress NOTICE messages during table rewrite operations.
SET client_min_messages TO WARNING;
CREATE TABLE base (col int, col2 int);
CREATE INDEX base_idx ON base(col2);
INSERT INTO base VALUES (1, 3), (2, 2), (3, 1);
ALTER TABLE base ADD PRIMARY KEY (col HASH); -- should fail.
ALTER TABLE base
    ADD PRIMARY KEY (col), ADD COLUMN col3 float DEFAULT random();
ALTER TABLE base ALTER COLUMN col2 TYPE int2;
ALTER TABLE base ADD COLUMN col4 SERIAL;
SELECT col, col2, col4 FROM base;
SELECT col, col2, col4 FROM base WHERE col2 = 1;
ALTER TABLE base DROP CONSTRAINT base_pkey;
SELECT col, col2, col4 FROM base ORDER BY col;
\d+ base;
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('base_idx'::regclass);
CREATE TABLE base2 (col int, col2 int) WITH (COLOCATION=false);
CREATE INDEX base2_idx ON base2(col2);
INSERT INTO base2 VALUES (1, 3), (2, 2), (3, 1);
ALTER TABLE base2
    ADD PRIMARY KEY (col ASC), ADD COLUMN col3 float DEFAULT random();
ALTER TABLE base2 ALTER COLUMN col2 TYPE int2;
ALTER TABLE base2 ADD COLUMN col4 SERIAL;
SELECT col, col2, col4 FROM base2;
SELECT col, col2, col4 FROM base2 WHERE col2 = 1;
ALTER TABLE base2 DROP CONSTRAINT base2_pkey;
SELECT col, col2, col4 FROM base2 ORDER BY col;
\d+ base2;
SELECT num_tablets, num_hash_key_columns, is_colocated FROM
    yb_table_properties('base2_idx'::regclass);
\c yugabyte;
-- Suppress NOTICE messages during table rewrite operations.
SET client_min_messages TO WARNING;
-- Test rewrite with tablegroups.
CREATE TABLEGROUP tg1;
CREATE TABLE test_tablegroup (id int) TABLEGROUP tg1;
INSERT INTO test_tablegroup VALUES (1), (2);
CREATE TABLE test_tablegroup2 (id int, id2 int UNIQUE WITH
    (colocation_id=100501)) WITH (colocation_id=100500) TABLEGROUP tg1;
INSERT INTO test_tablegroup2 VALUES (1, 1), (2, 2);
ALTER TABLE test_tablegroup ADD PRIMARY KEY (id ASC);
ALTER TABLE test_tablegroup ADD COLUMN c SERIAL;
ALTER TABLE test_tablegroup2 ADD PRIMARY KEY (id ASC);
ALTER TABLE test_tablegroup2 ADD COLUMN c SERIAL;
SELECT * FROM test_tablegroup;
SELECT * FROM test_tablegroup2;
\d test_tablegroup;
\d test_tablegroup2;
ALTER TABLE test_tablegroup DROP CONSTRAINT test_tablegroup_pkey;
ALTER TABLE test_tablegroup2 DROP CONSTRAINT test_tablegroup2_pkey;
SELECT * FROM test_tablegroup ORDER BY id;
SELECT * FROM test_tablegroup2 ORDER BY id;
\d test_tablegroup;
\d test_tablegroup2;

-- Tests for ALTER TABLE ... ADD COLUMN rewrite operations.
-- verify split options are preserved.
CREATE TABLE test_add_column(a int, b int)
    SPLIT INTO 2 TABLETS;
CREATE INDEX test_add_column_idx ON test_add_column(b ASC)
    SPLIT AT VALUES ((5), (10));
INSERT INTO test_add_column VALUES (1, 1);
ALTER TABLE test_add_column ADD COLUMN c SERIAL;
ALTER TABLE test_add_column ADD COLUMN d SERIAL PRIMARY KEY;
SELECT num_tablets, num_hash_key_columns FROM
    yb_table_properties('test_add_column'::regclass);
SELECT yb_get_range_split_clause('test_add_column_idx'::regclass);
INSERT INTO test_add_column(a, d) VALUES (2, 1); -- should fail.
INSERT INTO test_add_column(a, d) VALUES (2, 2);
SELECT * FROM test_add_column;

-- Tests for ALTER TABLE ADD/DROP PRIMARY KEY.
-- basic tests.
CREATE TABLE nopk (id int, v int);
ALTER TABLE nopk ADD PRIMARY KEY (id);
ALTER TABLE nopk ADD PRIMARY KEY (id); -- should fail.
ALTER TABLE nopk DROP CONSTRAINT nopk_pkey;
INSERT INTO nopk VALUES (1, 1);
INSERT INTO nopk VALUES (1, 2);
ALTER TABLE nopk ADD PRIMARY KEY (id); -- should fail.
DELETE FROM nopk WHERE v = 2;
ALTER TABLE nopk ADD PRIMARY KEY (id);
ALTER TABLE nopk DROP CONSTRAINT nopk_pkey;
ALTER TABLE nopk ALTER COLUMN id DROP NOT NULL;
INSERT INTO nopk VALUES (null);
ALTER TABLE nopk ADD PRIMARY KEY (id); -- should fail.
DROP TABLE nopk;
-- test complex pks.
CREATE TABLE complex_pk (v1 int, v2 text, v3 char, v4 boolean);
INSERT INTO complex_pk VALUES (1, '111', '1', 'true'), (2, '222', '2', 'false');
ALTER TABLE complex_pk ADD PRIMARY KEY ((v1, v2) HASH, v3 ASC, v4 DESC);
SELECT * FROM yb_table_properties('complex_pk'::regclass);
INSERT INTO complex_pk VALUES (2, '222', '3', 'true');
INSERT INTO complex_pk VALUES (2, '222', '2', 'false'); -- should fail.
SELECT * FROM complex_pk ORDER BY v1, v2, v3, v4;
ALTER TABLE complex_pk DROP CONSTRAINT complex_pk_pkey;
INSERT INTO complex_pk VALUES (2, '222', '3', 'true');
SELECT * FROM complex_pk ORDER BY v1, v2, v3, v4;
-- test range pks.
CREATE TABLE range_pk (id int);
INSERT INTO range_pk VALUES (1), (2), (3);
ALTER TABLE range_pk ADD PRIMARY KEY (id DESC);
SELECT * FROM range_pk; -- should be in descending order.
ALTER TABLE range_pk DROP CONSTRAINT range_pk_pkey;
SELECT * FROM range_pk ORDER BY id;
-- test include pks.
CREATE TABLE include_pk (id int, v1 int, v2 int);
INSERT INTO include_pk VALUES (1, 11, 111), (2, 22, 222);
ALTER TABLE include_pk ADD PRIMARY KEY (id) INCLUDE (v1, v2);
INSERT INTO include_pk VALUES (3, 11, 111);
EXPLAIN SELECT v1 FROM include_pk WHERE id = 2;
SELECT v1 FROM include_pk WHERE id = 2;
INSERT INTO include_pk VALUES (3, 99, 999); -- should fail.
SELECT * FROM include_pk ORDER BY id;
EXPLAIN SELECT v1 FROM include_pk WHERE id = 3;
SELECT v1 FROM include_pk WHERE id = 3;
ALTER TABLE include_pk DROP CONSTRAINT include_pk_pkey;
EXPLAIN SELECT v1 FROM include_pk WHERE id = 3;
SELECT v1 FROM include_pk WHERE id = 3;
-- test pk with UDT.
CREATE TYPE typeid AS (i int);
CREATE TABLE nopk_udt (id typeid, v int);
ALTER TABLE nopk_udt ADD PRIMARY KEY (id); -- should fail.
-- test pk USING INDEX.
CREATE TABLE nopk_usingindex (id int);
CREATE UNIQUE INDEX nopk_idx ON nopk_usingindex (id ASC);
ALTER TABLE nopk_usingindex ADD PRIMARY KEY USING INDEX nopk_idx; -- should fail.
-- test adding/dropping pks on partitioned tables.
CREATE TABLE nopk_whole (id int) PARTITION BY LIST (id);
CREATE TABLE nopk_part1 PARTITION OF nopk_whole FOR VALUES IN (1, 2, 3);
CREATE TABLE nopk_part2 PARTITION OF nopk_whole FOR VALUES IN (10, 20, 30, 40)
    PARTITION BY LIST (id);
CREATE TABLE nopk_part2_part1 PARTITION OF nopk_part2 FOR VALUES IN (10, 20);
CREATE TABLE nopk_part2_part2 PARTITION OF nopk_part2 FOR VALUES IN (30, 40);
ALTER TABLE nopk_whole ADD PRIMARY KEY (id);
-- verify that we cannot drop inherited PK constraints.
ALTER TABLE nopk_part1 DROP CONSTRAINT nopk_part1_pkey; -- should fail.
ALTER TABLE nopk_part2 DROP CONSTRAINT nopk_part2_pkey; -- should fail.
ALTER TABLE nopk_part2_part1 DROP CONSTRAINT nopk_part2_part1_pkey; -- should fail.
ALTER TABLE nopk_part2_part2 DROP CONSTRAINT nopk_part2_part2_pkey; -- should fail.
ALTER TABLE nopk_whole DROP CONSTRAINT nopk_whole_pkey;
-- verify that we can successfully add and drop PK constraints on partitions.
ALTER TABLE nopk_part1 ADD PRIMARY KEY (id);
ALTER TABLE nopk_part1 DROP CONSTRAINT nopk_part1_pkey;
ALTER TABLE nopk_part2 ADD PRIMARY KEY (id);
ALTER TABLE nopk_part2 DROP CONSTRAINT nopk_part2_pkey;
ALTER TABLE nopk_part2_part1 ADD PRIMARY KEY (id);
ALTER TABLE nopk_part2_part1 DROP CONSTRAINT nopk_part2_part1_pkey;
ALTER TABLE nopk_part2_part2 ADD PRIMARY KEY (id);
ALTER TABLE nopk_part2_part2 DROP CONSTRAINT nopk_part2_part2_pkey;
-- tests for altered table referenced by a partitioned FK table.
CREATE TABLE test (id int unique);
CREATE TABLE test_part (id int REFERENCES test(id)) PARTITION BY RANGE(id);
CREATE TABLE test_part_1 PARTITION OF test_part FOR VALUES FROM (1) TO (100);
INSERT INTO test VALUES (1);
INSERT INTO test_part VALUES (1);
ALTER TABLE test ADD PRIMARY KEY (id);
SELECT * FROM test;
SELECT * FROM test_part ORDER BY id;
INSERT INTO test_part VALUES (2); -- should fail.
INSERT INTO test_part_1 VALUES (2); -- should fail.
DROP TABLE test CASCADE;
-- tests for split options.
-- verify split options are preserved when we add/drop a hash key.
CREATE TABLE nopk (id int, a int, b text, c float, d timestamp, e money)
    SPLIT INTO 5 TABLETS;
CREATE INDEX nopk_idx1 ON nopk(id) SPLIT INTO 4 TABLETS;
CREATE INDEX nopk_idx2 ON nopk(id ASC, a ASC, b ASC, c ASC, d ASC, e ASC)
    SPLIT AT VALUES
    ((10, 20, E'test123\"\"''\\\\\\u0068\\u0069', '-Infinity', '1999-01-01',
    '12.34'),
    (20, 30, E'test123\"\"''\\\\\\u0068\\u0069z', 'Infinity', '2023-01-01',
    '56.78'));
ALTER TABLE nopk ADD PRIMARY KEY (id);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('nopk'::regclass);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('nopk_idx1'::regclass);
SELECT yb_get_range_split_clause('nopk_idx2'::regclass);
ALTER TABLE nopk DROP CONSTRAINT nopk_pkey;
-- verify split options are not preserved when we add a range key, and only the number of
-- tablets is preserved when we drop a range key.
CREATE TABLE nopk2 (id int) SPLIT INTO 5 TABLETS;
ALTER TABLE nopk2 ADD PRIMARY KEY (id);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('nopk2'::regclass);
CREATE TABLE range_pk_test (id int, primary key(id asc)) SPLIT AT VALUES ((5), (10), (15), (20));
ALTER TABLE range_pk_test DROP CONSTRAINT range_pk_test_pkey;
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('range_pk_test'::regclass);
-- rules:
-- 1. new table rewrite + rule
-- 2. old table rewrite + rule -- fail
-- 3. new table rewrite + no more rule
-- 4. old table rewrite + no more rule
CREATE TABLE pk_rule_table (id int);
CREATE RULE pk_rule AS
    ON INSERT TO pk_rule_table
    DO INSTEAD DELETE FROM pk_rule_table;
ALTER TABLE pk_rule_table ADD PRIMARY KEY (id);
SET yb_enable_alter_table_rewrite = OFF;
ALTER TABLE pk_rule_table DROP CONSTRAINT pk_rule_table_pkey; -- should fail.
RESET yb_enable_alter_table_rewrite;
DROP RULE pk_rule ON pk_rule_table;
ALTER TABLE pk_rule_table DROP CONSTRAINT pk_rule_table_pkey;
SET yb_enable_alter_table_rewrite = OFF;
ALTER TABLE pk_rule_table ADD PRIMARY KEY (id);
RESET yb_enable_alter_table_rewrite;

-- Tests for ALTER TYPE.
-- basic tests.
CREATE TABLE varchar_table(id SERIAL, c1 varchar(10), PRIMARY KEY (id ASC));
CREATE TABLE int4_table(id SERIAL, c1 int4, PRIMARY KEY (id ASC));
ALTER TABLE varchar_table ALTER c1 TYPE int; -- should fail.
ALTER TABLE int4_table ALTER c1 TYPE int8;
INSERT INTO int4_table(c1) VALUES (2 ^ 40);
ALTER TABLE int4_table ALTER c1 TYPE int4; -- should fail.
INSERT INTO varchar_table(c1) VALUES ('aa');
ALTER TABLE varchar_table ALTER c1 TYPE varchar(1); -- should fail.
INSERT INTO varchar_table(c1) VALUES ('a'), (2), ('aaa');
ALTER TABLE varchar_table ALTER c1 TYPE text;
SELECT * from varchar_table;
INSERT INTO varchar_table(c1) VALUES ('a'), ('bb'), ('ccc');
ALTER TABLE varchar_table ALTER c1 TYPE int USING length(c1);
SELECT * from varchar_table;
ALTER TABLE varchar_table ALTER c1 TYPE double precision USING sqrt(c1);
SELECT * from varchar_table;
ALTER TABLE int4_table ADD COLUMN c2 varchar, ADD COLUMN c3 int, ALTER c1 TYPE varchar;
INSERT INTO int4_table(c1, c2, c3) VALUES ('a', 'a', 1), ('b', 'b', 2), ('c', 'c', 3);
SELECT * from int4_table;
CREATE TABLE int4_table2(c1 int) PARTITION BY RANGE(c1);
ALTER TABLE int4_table2 ALTER c1 TYPE varchar; -- should fail.
CREATE TABLE pk_table(c1 int primary key);
INSERT INTO pk_table VALUES (1), (2);
ALTER TABLE pk_table ALTER c1 TYPE varchar;
SELECT * from pk_table ORDER BY c1 ASC;
INSERT INTO pk_table VALUES (1); -- should fail.
ALTER TABLE pk_table ALTER c1 TYPE int USING length(c1);  -- should fail.
-- test ALTER TYPE on a column that is a part of a view/rule:
-- 1. column part of view --fail
-- 2. column not part of view
-- 3. column part of view via _RETURN rule -- fail
-- 4. column not part of view via _RETURN rule
-- 5. rule on other table/view that affects this table
-- 6. rule on other table/view + rule on this table
-- 7. rule on other table/view + rule on this table + old rewrite -- fail
-- 8. rule on other table/view + no more rule on this table
-- 9. rule on other table/view (this is dropped for #22063) + no more rule on this table + old rewrite
CREATE TABLE test_table (c1 int, c2 varchar, c3 varchar, c4 varchar);
CREATE VIEW test_view AS SELECT c2 FROM test_table;
ALTER TABLE test_table ALTER c2 TYPE int USING length(c2); -- should fail.
ALTER TABLE test_table ALTER c3 TYPE varchar(1);
CREATE TABLE dummy_table (c3 varchar);
CREATE RULE "_RETURN" AS
    ON SELECT TO dummy_table
    DO INSTEAD
        SELECT c3 FROM test_table;
ALTER TABLE test_table ALTER c3 TYPE varchar(1); -- should fail.
ALTER TABLE test_table ALTER c4 TYPE varchar(1);
CREATE RULE dummy_rule AS
    ON INSERT TO dummy_table
    DO INSTEAD DELETE FROM test_table;
ALTER TABLE test_table ALTER c4 TYPE char;
CREATE RULE test_rule AS
    ON INSERT TO test_table
    DO INSTEAD DELETE FROM dummy_table;
ALTER TABLE test_table ALTER c4 TYPE varchar(1);
SET yb_enable_alter_table_rewrite = OFF;
ALTER TABLE test_table ALTER c4 TYPE char; -- should fail.
RESET yb_enable_alter_table_rewrite;
DROP RULE test_rule ON test_table;
ALTER TABLE test_table ALTER c4 TYPE char;
SET yb_enable_alter_table_rewrite = OFF;
-- TODO(#22063): DROP VIEW is needed to avoid non-deterministic error.
DROP VIEW dummy_table;
ALTER TABLE test_table ALTER c4 TYPE varchar(1);
RESET yb_enable_alter_table_rewrite;
DROP TABLE test_table CASCADE;
-- test ALTER TYPE on a foreign key column.
CREATE TABLE fk_table (c1 varchar primary key);
CREATE TABLE test_table (c1 varchar primary key references fk_table(c1));
CREATE TABLE fk_table2 (c1 varchar references test_table(c1));
INSERT INTO fk_table VALUES ('a'), ('aa'), ('aaa');
INSERT INTO test_table VALUES ('a'), ('aa'), ('aaa');
INSERT INTO fk_table2 VALUES ('a'), ('aa'), ('aaa');
ALTER TABLE fk_table ALTER c1 TYPE int USING LENGTH(c1); -- should fail.
ALTER TABLE fk_table ALTER c1 TYPE varchar(3);
\d fk_table;
\d test_table;
\d fk_table2;
INSERT INTO fk_table2 VALUES ('aaaa'); -- should fail.
INSERT INTO test_table VALUES ('aaaa'); -- should fail.
DROP TABLE test_table CASCADE;
-- test ALTER TYPE where an index with a default tablespace is re-created.
CREATE TABLESPACE test_tblspc LOCATION '/invalid';
SET default_tablespace = test_tblspc;
CREATE TABLE test_table (c1 int, c2 int);
CREATE INDEX test_index ON test_table(c1);
ALTER TABLE test_table ALTER c1 TYPE int2;
\d test_index;
-- test ALTER TYPE on a table with a range key.
CREATE TABLE range_key_table(col1 int, a int, b int, col2 int, c text, d text,
    col3 int, e int, PRIMARY KEY((a, b) HASH, c ASC, d DESC) INCLUDE (e));
INSERT INTO range_key_table(a, b, c, d)
    VALUES (1, 1, 'a', 'e'), (1, 1, 'aa', 'f'), (1, 1, 'aa', 'g');
CREATE INDEX ON range_key_table(c ASC);
ALTER TABLE range_key_table ALTER c TYPE int USING length(c);
SELECT a, b, c, d FROM range_key_table;
\d range_key_table;
-- tests for split options.
CREATE TABLE test1 (c1 varchar, c2 varchar) SPLIT INTO 5 TABLETS;
CREATE INDEX idx1 ON test1(c1) SPLIT INTO 5 TABLETS;
CREATE INDEX idx2 ON test1(c1 HASH, c2 ASC) SPLIT INTO 5 TABLETS;
CREATE INDEX idx3 ON test1(c2 ASC) INCLUDE(c1)
    SPLIT AT VALUES ((E'test123\"\"''\\\\\\u0068\\u0069'));
CREATE INDEX idx4 ON test1(c1 ASC) SPLIT AT VALUES (('h'));
ALTER TABLE test1 ALTER c1 TYPE int USING length(c1);
-- verify hash split options on the table are preserved.
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('test1'::regclass);
-- verify hash split options on the indexes are preserved.
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('idx1'::regclass);
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('idx2'::regclass);
-- verify range split options on the indexes are only preserved when the altered column is not a
-- part of the index key.
SELECT yb_get_range_split_clause('idx3'::regclass);
SELECT yb_get_range_split_clause('idx4'::regclass);
CREATE TABLE test2 (c1 varchar, c2 varchar, PRIMARY KEY(c1 ASC, c2 DESC))
    SPLIT AT VALUES (('h', 20));
ALTER TABLE test2 ALTER c1 TYPE int USING length(c1);
CREATE TABLE test3 (c1 varchar, c2 varchar, PRIMARY KEY(c2 ASC))
    SPLIT AT VALUES ((E'test123\"\"''\\\\\\u0068\\u0069'));
ALTER TABLE test3 ALTER c1 TYPE int USING length(c1);
-- verify range split options on the table are only preserved when the altered column is not a
-- part of the table's key.
SELECT yb_get_range_split_clause('test2'::regclass);
SELECT yb_get_range_split_clause('test3'::regclass);
-- test ALTER TYPE on temp tables.
CREATE TEMP TABLE test_table (col1 text UNIQUE);
INSERT INTO test_table VALUES ('1'), ('01');
ALTER TABLE test_table ALTER COLUMN col1 TYPE integer using col1::int;
ALTER TABLE test_table DROP CONSTRAINT test_table_col1_key;
ALTER TABLE test_table ALTER COLUMN col1 TYPE integer using col1::int;
SELECT * FROM test_table;

--
-- Test ALTER TABLE ... ADD COLUMN ... GENERATED ALWAYS AS IDENTITY
--
CREATE TABLE test_identity (id int, PRIMARY KEY (id ASC));
INSERT INTO test_identity VALUES (1), (2), (3);
ALTER TABLE test_identity ADD COLUMN id2 int GENERATED ALWAYS AS IDENTITY;
INSERT INTO test_identity VALUES (4, 4); -- should fail
INSERT INTO test_identity OVERRIDING SYSTEM VALUE VALUES (4, 4);
SELECT * FROM test_identity;

--
-- Test ALTER TABLE ... DROP COLUMN on a primary key column.
--
-- basic test
CREATE TABLE test_drop_pk_column (id int PRIMARY KEY, v int);
INSERT INTO test_drop_pk_column VALUES (1, 1), (2, 2), (3, 3);
ALTER TABLE test_drop_pk_column DROP COLUMN id;
\d test_drop_pk_column;
INSERT INTO test_drop_pk_column VALUES (1);
SELECT * FROM test_drop_pk_column ORDER BY v;
-- test composite PK
CREATE TABLE test_drop_pk_column_composite (id int, v int, t int, PRIMARY KEY (id HASH, v ASC));
INSERT INTO test_drop_pk_column_composite VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);
ALTER TABLE test_drop_pk_column_composite DROP COLUMN id;
\d test_drop_pk_column_composite;
INSERT INTO test_drop_pk_column_composite VALUES (1, 1);
SELECT * FROM test_drop_pk_column_composite ORDER BY v;
-- test partitioned table
CREATE TABLE test_drop_pk_column_part (id int PRIMARY KEY, v int) PARTITION BY RANGE (id);
CREATE TABLE test_drop_pk_column_part1 PARTITION OF test_drop_pk_column_part FOR VALUES FROM (1) TO (10);
ALTER TABLE test_drop_pk_column_part DROP COLUMN id; -- should fail.
