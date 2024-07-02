---
--- Verify renaming on temp tables
---
CREATE TEMP TABLE temp_table(a int primary key, b int);
CREATE INDEX temp_table_b_idx ON temp_table(b);
ALTER INDEX temp_table_pkey RENAME TO temp_table_pkey_new;
ALTER INDEX temp_table_b_idx RENAME TO temp_table_b_idx_new;

---
--- Verify yb_db_admin role can ALTER table
---
CREATE TABLE foo(a INT UNIQUE);
CREATE TABLE bar(b INT);
ALTER TABLE bar ADD CONSTRAINT baz FOREIGN KEY (b) REFERENCES foo(a);
CREATE TABLE table_other(a int, b int);
CREATE INDEX index_table_other ON table_other(a);
CREATE USER regress_alter_table_user1;
SET SESSION AUTHORIZATION yb_db_admin;
ALTER TABLE table_other RENAME to table_new;
ALTER TABLE table_new OWNER TO regress_alter_table_user1;
ALTER TABLE bar DROP CONSTRAINT baz;
ALTER TABLE pg_database RENAME TO test; -- should fail
ALTER TABLE pg_tablespace OWNER TO regress_alter_table_user1; -- should fail
---
--- Verify yb_db_admin role can ALTER index
---
ALTER INDEX index_table_other RENAME TO index_table_other_new;
RESET SESSION AUTHORIZATION;
DROP TABLE foo;
DROP TABLE bar;
DROP TABLE table_new;
DROP USER regress_alter_table_user1;

---
--- Verify alter table which requires table rewrite
---

--- Table without primary key index
--- Empty table case
CREATE TABLE no_pk_tbl(k INT);
ALTER TABLE no_pk_tbl ADD COLUMN s1 TIMESTAMP DEFAULT clock_timestamp();
ALTER TABLE no_pk_tbl ADD COLUMN v1 SERIAL;
\d no_pk_tbl;
--- Non-empty case
INSERT INTO no_pk_tbl VALUES(1), (2), (3);
ALTER TABLE no_pk_tbl ADD COLUMN s2 TIMESTAMP DEFAULT clock_timestamp();
ALTER TABLE no_pk_tbl ADD COLUMN v2 SERIAL;
\d no_pk_tbl;
DROP TABLE no_pk_tbl;

--- Table with primary key index
--- Empty table case
CREATE TABLE pk_tbl(k INT PRIMARY KEY);
ALTER TABLE pk_tbl ADD COLUMN s1 TIMESTAMP DEFAULT clock_timestamp();
ALTER TABLE pk_tbl ADD COLUMN v1 SERIAL;
\d pk_tbl;
--- Non-empty case
INSERT INTO pk_tbl VALUES(1), (2), (3);
ALTER TABLE pk_tbl ADD COLUMN s2 TIMESTAMP DEFAULT clock_timestamp();
ALTER TABLE pk_tbl ADD COLUMN v2 SERIAL;
\d pk_tbl;
DROP TABLE pk_tbl;

-- Verify cache cleanup of table names when TABLE RENAME fails.
CREATE TABLE rename_test (id int);
SET yb_test_fail_next_ddl TO true;
ALTER TABLE rename_test RENAME TO foobar;
-- The table name must be unchanged.
SELECT * FROM rename_test;
-- The name 'foobar' must be invalid.
SELECT * FROM foobar;
-- Rename operation must succeed now.
ALTER TABLE rename_test RENAME TO foobar;
DROP TABLE foobar;

--
-- ALTER TABLE ADD COLUMN ... DEFAULT tests.
--
CREATE TABLE foo(a int);
INSERT INTO foo VALUES (1), (2), (3);
-- Test add column with int default value.
ALTER TABLE foo ADD COLUMN b int DEFAULT 6;
INSERT INTO foo(a) VALUES (4);
INSERT INTO foo VALUES (5, 7);
INSERT INTO foo VALUES (6, null);
SELECT * FROM foo ORDER BY a;
CREATE TYPE typefoo AS (a inet, b BIT(3));
-- Test add column with a UDT default value.
ALTER TABLE foo ADD COLUMN c typefoo DEFAULT ('127.0.0.1', B'010');
SELECT * FROM foo ORDER BY a;
CREATE FUNCTION functionfoo()
RETURNS TIMESTAMP
LANGUAGE plpgsql STABLE
AS
$$
BEGIN
RETURN '01-01-2023';
END;
$$;
-- Test add column with a non-volatile expression default value.
ALTER TABLE foo ADD COLUMN d TIMESTAMP DEFAULT functionfoo();
SELECT * FROM foo ORDER BY a;
-- Test add column with default value and collation
ALTER TABLE foo ADD COLUMN e varchar DEFAULT 'hi' COLLATE "en_US";
INSERT INTO foo(a, e) VALUES(7, 'a');
INSERT INTO foo(a, e) VALUES(8, 'zz');
SELECT * FROM foo ORDER BY e, a;
SELECT * FROM foo WHERE e COLLATE "C" < 'hi' ORDER BY e;
-- Test updating columns that have missing default values.
UPDATE foo SET d = '01-01-2024' WHERE a = 1;
SELECT * FROM foo ORDER BY a;
UPDATE foo SET b = 8 WHERE b is null;
SELECT * FROM foo WHERE b = 8;
UPDATE foo SET b = null WHERE b = 8;
SELECT * FROM foo WHERE b is null;
-- Test expression pushdown on column with default value.
EXPLAIN SELECT * FROM foo WHERE d = '01-01-2023';
SELECT * FROM foo WHERE b = 6 ORDER BY a;
-- Verify that we set pg_attribute.atthasmissing and
-- pg_attribute.attmissingval.
SELECT atthasmissing, attmissingval FROM pg_attribute
    WHERE attrelid='foo'::regclass;
-- Verify that ALTER TABLE ... SET DEFAULT doesn't change missing values.
ALTER TABLE foo ALTER COLUMN b SET DEFAULT 7;
INSERT INTO foo(a) VALUES (9);
SELECT * FROM foo ORDER BY a;
-- Verify that indexes on columns with missing default values work.
CREATE INDEX ON foo(b);
EXPLAIN SELECT b FROM foo WHERE b = 6;
SELECT b FROM foo WHERE b = 6;
EXPLAIN SELECT * FROM foo WHERE b = 6 ORDER BY a;
SELECT * FROM foo WHERE b = 6 ORDER BY a;
-- Verify that defaults are copied for tables created using CREATE TABLE LIKE
-- clause.
CREATE TABLE dummy (LIKE foo INCLUDING DEFAULTS);
INSERT INTO dummy(a) VALUES (1);
SELECT * FROM dummy;
-- Verify that missing values work after table rewrite.
ALTER TABLE foo ADD PRIMARY KEY (a);
SELECT * FROM foo ORDER BY a;
-- Verify missing default values for partitioned tables.
CREATE TABLE foo_part (a int) PARTITION BY RANGE (a);
CREATE TABLE foo_part_1 PARTITION OF foo_part FOR VALUES FROM (1) TO (6);
CREATE TABLE foo_part_2 PARTITION OF foo_part FOR VALUES FROM (6) TO (11);
INSERT INTO foo_part VALUES (generate_series(1, 10));
CREATE FUNCTION functionfoopart()
RETURNS TEXT
LANGUAGE plpgsql STABLE
AS
$$
BEGIN
RETURN 'default';
END;
$$;
ALTER TABLE foo_part ADD COLUMN b TEXT default functionfoopart();
INSERT INTO foo_part VALUES (1, null), (6, null);
SELECT * FROM foo_part ORDER BY a, b;
-- Verify that ADD COLUMN ... DEFAULT NOT NULL fails when the default value is
-- null.
ALTER TABLE foo ADD COLUMN g int DEFAULT null NOT NULL;
-- Test add column with volatile default value.
ALTER TABLE foo ADD COLUMN f FLOAT DEFAULT random();
--
-- Test for cascaded drops on columns
--
CREATE TABLE test_dropcolumn(a int, b int, c int);
CREATE TYPE test_dropcolumn_type AS (a int, b int);
ALTER TABLE test_dropcolumn ADD COLUMN d test_dropcolumn_type;
DROP TYPE test_dropcolumn_type CASCADE; -- should drop the column d
ALTER TABLE test_dropcolumn ADD COLUMN d int;
