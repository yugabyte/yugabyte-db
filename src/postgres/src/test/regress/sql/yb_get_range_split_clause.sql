-- Tests for yb_get_range_split_clause: Verify the new YB function to get correct
-- range-split SPLIT AT (%s) clause.

\pset null '(null)'

-- Test edge cases & system tables
SELECT yb_get_range_split_clause(null);

SELECT yb_get_range_split_clause(0);

SELECT yb_get_range_split_clause('pg_class'::regclass);

-- Control the message level sent to the client to make this test suite more robust for oid changes.
-- Don't output NOTICE: relation with oid <oid> is not backed by YB for some cases.
SET client_min_messages = 'WARNING';

-- Test system view, view, YB materialized view
SELECT yb_get_range_split_clause('pg_roles'::regclass);

CREATE TABLE view_test (a INT);
CREATE VIEW my_view AS SELECT * FROM view_test;
SELECT yb_get_range_split_clause('my_view'::regclass);
DROP VIEW my_view;

CREATE MATERIALIZED VIEW my_materialized_view AS SELECT * FROM view_test;
SELECT yb_get_range_split_clause('my_materialized_view'::regclass);
DROP MATERIALIZED VIEW my_materialized_view;
DROP TABLE view_test;

RESET client_min_messages;
SET yb_enable_create_with_table_oid = true;

-- Test temp table
CREATE TEMP TABLE temp_tbl (
  a INT,
  PRIMARY KEY(a ASC)
) WITH (table_oid = 20000) SPLIT AT VALUES((-100), (250));
SELECT yb_get_range_split_clause('temp_tbl'::regclass);
DROP TABLE temp_tbl;

-- Test table with no primary key.
CREATE TABLE no_pk_tbl (
  a INT,
  b TEXT
) WITH (table_oid = 20003);
SELECT yb_get_range_split_clause('no_pk_tbl'::regclass);
DROP TABLE no_pk_tbl;

-- Test hash-partitioned table.
CREATE TABLE hash_partitioned_tbl (k INT PRIMARY KEY) WITH (table_oid = 20006) SPLIT INTO 5 TABLETS;
SELECT yb_get_range_split_clause('hash_partitioned_tbl'::regclass);
DROP TABLE hash_partitioned_tbl;

-- Test range-partitioned table.
-- Test range-partitioned table with no SPLIT AT clause
CREATE TABLE range_no_split_at_tbl (
  a INT,
  PRIMARY KEY(a ASC)
);
SELECT yb_get_range_split_clause('range_no_split_at_tbl'::regclass);
DROP TABLE range_no_split_at_tbl;

-- Test basic cases
CREATE TABLE tbl (
  a INT,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES((-100), (250));
SELECT yb_get_range_split_clause('tbl'::regclass);
DROP TABLE tbl;

CREATE TABLE tbl2 (
 a TEXT,
 b INT,
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES(('bar'), ('foo'));
SELECT yb_get_range_split_clause('tbl2'::regclass);
DROP TABLE tbl2;

CREATE TABLE tbl_with_many_numeric_types (
 a SMALLINT,
 b BIGINT,
 c REAL,
 d DOUBLE PRECISION,
 e DECIMAL,
 f NUMERIC,
 g SMALLSERIAL,
 h SERIAL,
 i BIGSERIAL,
 PRIMARY KEY(a ASC, b ASC, c ASC, d ASC, e ASC, f ASC, g ASC, h ASC, i ASC)
) SPLIT AT VALUES((100, 100, 100, 100, 100, 100, 100, 100, 100));
SELECT yb_get_range_split_clause('tbl_with_many_numeric_types'::regclass);
DROP TABLE tbl_with_many_numeric_types;

CREATE TABLE tbl_with_many_character_types (
 a CHARACTER VARYING(10),
 b VARCHAR(10),
 c CHARACTER(10),
 d CHAR(10),
 e TEXT,
 PRIMARY KEY(a ASC, b ASC, c ASC, d ASC, e ASC)
) SPLIT AT VALUES(('a', 'bb', 'ccc', 'dddd', 'eeeee'));
SELECT yb_get_range_split_clause('tbl_with_many_character_types'::regclass);
DROP TABLE tbl_with_many_character_types;

-- Test MIN & MAX boundary case
CREATE TABLE tbl3 (
  a INT,
  b INT,
  c INT,
  d INT,
  PRIMARY KEY(a ASC, b ASC, c ASC, d ASC)
) SPLIT AT VALUES((100), (200, MINVALUE, MINVALUE, 5), (400, MAXVALUE));
SELECT yb_get_range_split_clause('tbl3'::regclass);
DROP TABLE tbl3;

-- Test type cast case
CREATE TABLE tbl4 (
  a TEXT,
  b INT,
  c INT,
  d TEXT,
  PRIMARY KEY(a ASC, b ASC, c ASC, d ASC)
) SPLIT AT VALUES((100), (200, MINVALUE, MINVALUE, 5), (400, MAXVALUE));
SELECT yb_get_range_split_clause('tbl4'::regclass);
DROP TABLE tbl4;

-- Test monetary type
CREATE TABLE tbl5 (
  a MONEY,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('$100.99'));
SELECT yb_get_range_split_clause('tbl5'::regclass);
DROP TABLE tbl5;

-- Test binary type
CREATE TABLE tbl6 (
  a BYTEA,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('value'));
SELECT yb_get_range_split_clause('tbl6'::regclass);
SELECT 'value'::bytea; -- verify the same binary string output
DROP TABLE tbl6;

CREATE TABLE escape_binary (
  a BYTEA,
  PRIMARY KEY (a ASC)
) SPLIT AT VALUES ((E'\x7F\U0001ffff'));
SELECT yb_get_range_split_clause('escape_binary'::regclass);
SELECT E'\x7F\U0001ffff'::bytea = E'\\x7ff09fbfbf'::bytea;
DROP TABLE escape_binary;

-- Test date time type
SET timezone = '0';
SET datestyle = 'ISO';

CREATE TABLE tbl_with_many_date_time_types (
  a TIMESTAMP(2),
  b TIMESTAMP WITH TIME ZONE,
  c DATE,
  d TIME(2),
  PRIMARY KEY(a ASC, b ASC, c ASC, d ASC)
) SPLIT AT VALUES (('1234-01-30 07:08:09', '1234-01-30 07:08:09+06', '1234-01-30', '07:08:09'));
SELECT yb_get_range_split_clause('tbl_with_many_date_time_types'::regclass);

SET timezone = '2';
SELECT yb_get_range_split_clause('tbl_with_many_date_time_types'::regclass);

DROP TABLE tbl_with_many_date_time_types;
RESET timezone;
RESET datestyle;

-- Test boolean type
CREATE TABLE tbl7 (
  a BOOLEAN,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('true'));
SELECT yb_get_range_split_clause('tbl7'::regclass);
DROP TABLE tbl7;

-- Test uuid type
CREATE TABLE tbl8 (
  a UUID,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('12345678-1234-5678-1234-567812345678'));
SELECT yb_get_range_split_clause('tbl8'::regclass);
DROP TABLE tbl8;

-- Test object identifier type
CREATE TABLE tbl_with_many_object_identifier_types (
  a OID,
  b REGPROC,
  c REGPROCEDURE,
  d REGOPER,
  e REGOPERATOR,
  f REGCLASS,
  g REGTYPE,
  h REGROLE,
  i REGNAMESPACE,
  j REGCONFIG,
  k REGDICTIONARY,
  PRIMARY KEY(a ASC, b ASC, c ASC, d ASC, e ASC, f ASC, g ASC, h ASC, i ASC, j ASC, k ASC)
) SPLIT AT VALUES ((100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100));
SELECT yb_get_range_split_clause('tbl_with_many_object_identifier_types'::regclass);
DROP TABLE tbl_with_many_object_identifier_types;

-- Test pg_lsn type
CREATE TABLE tbl9 (
  a PG_LSN,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('1/2345678'));
SELECT yb_get_range_split_clause('tbl9'::regclass);
DROP TABLE tbl9;

-- Test Infinity
CREATE TABLE tbl_floating_point_infinity (
  a DOUBLE PRECISION,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('-Infinity'), (100), (200), ('Infinity'));
SELECT yb_get_range_split_clause('tbl_floating_point_infinity'::regclass);
DROP TABLE tbl_floating_point_infinity;

-- Test NaN
CREATE TABLE tbl_floating_point_nan (
  a DOUBLE PRECISION,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES ((100), (200), ('NaN'));
SELECT yb_get_range_split_clause('tbl_floating_point_nan'::regclass);
DROP TABLE tbl_floating_point_nan;
-- verify the correctness of split points order
CREATE TABLE tbl_has_nan_value (
  a DOUBLE PRECISION
);
INSERT INTO tbl_has_nan_value VALUES ('NAN'), (100), (200);
SELECT * FROM tbl_has_nan_value ORDER BY a ASC;
DROP TABLE tbl_has_nan_value;

CREATE TABLE tbl_numeric_nan (
  a NUMERIC,
  PRIMARY KEY(a DESC)
) SPLIT AT VALUES (('NaN'), (200), (100));

-- Test Enumerated type
CREATE TYPE enum_type AS ENUM('one', 'two', 'three', 'four', 'five', 'six', 'seven');
CREATE TABLE tbl_enum (
  a enum_type,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES (('two'), ('four'), ('six'));
SELECT yb_get_range_split_clause('tbl_enum'::regclass);
DROP TABLE tbl_enum;
DROP TYPE enum_type;

-- Test strings containing single-quoted, double-quoted, backslashes
CREATE TABLE tbl_with_special_strings (
 a TEXT,
 PRIMARY KEY(a DESC)
) SPLIT AT VALUES(('D si''ngle quo''tes'), ('C double" quotes'), (E'B back\\slashes'), (E'A all'' combo\\ of them"'));
SELECT yb_get_range_split_clause('tbl_with_special_strings'::regclass);
DROP TABLE tbl_with_special_strings;

-- When standard_conforming_strings = ON, backslash is treated as literal character.
-- standard_conforming_strings = ON is the default value
CREATE TABLE tbl_with_special_strings2 (
 a TEXT,
 PRIMARY KEY(a DESC)
) SPLIT AT VALUES(('D si''ngle quo''tes'), ('C double" quotes'), ('B back\slashes'), ('A all'' combo\ of them"'));
SELECT yb_get_range_split_clause('tbl_with_special_strings2'::regclass);
DROP TABLE tbl_with_special_strings2;

-- When standard_conforming_strings = OFF, backslashs are treated as escapes.
SET standard_conforming_strings = OFF;

-- Incorrect escapes ignore backslashs
SELECT 'B back\slashes';
SELECT E'B back\slashes';

CREATE TABLE tbl_with_special_strings3 (
 a TEXT,
 PRIMARY KEY(a DESC)
) SPLIT AT VALUES(('D si''ngle quo''tes'), ('C double" quotes'), ('B back\slashes'), ('A all'' combo\ of them"'));
SELECT yb_get_range_split_clause('tbl_with_special_strings3'::regclass);
DROP TABLE tbl_with_special_strings3;

-- Test proper escapes
CREATE TABLE tbl_with_special_strings4 (
 a TEXT,
 PRIMARY KEY(a DESC)
) SPLIT AT VALUES((E'C\tC'), (E'B\x42'), (E'A\\A'));
SELECT yb_get_range_split_clause('tbl_with_special_strings4'::regclass);
DROP TABLE tbl_with_special_strings4;

CREATE TABLE tbl_with_special_strings5 (
 a TEXT,
 PRIMARY KEY(a DESC)
) SPLIT AT VALUES((E'A\nB'));
SELECT yb_get_range_split_clause('tbl_with_special_strings5'::regclass);

-- verify correctness of tbl_with_special_strings5
SELECT yb_get_range_split_clause('tbl_with_special_strings5'::regclass)::TEXT = 'SPLIT AT VALUES ((''' || E'A\nB' || '''))';
DROP TABLE tbl_with_special_strings5;

RESET standard_conforming_strings;

-- Test multibyte character
-- Note: \u7F57 = '罗' = 	\xe7\xbd\x97, \u7FBD = '羽' = \xe7\xbe\xbd, \u8000 = '耀' = \xe8\x80\x80
CREATE TABLE tbl_multibyte (
 a TEXT,
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES((E'\u7F57'), ('羽'), (E'\xe8\x80\x80'));
SELECT yb_get_range_split_clause('tbl_multibyte'::regclass);

-- Test index
CREATE TABLE tbl10 (a TEXT);
CREATE INDEX tbl10_idx ON tbl10 (
  a ASC
) SPLIT AT VALUES (('bar'), ('foo'), ('qux'));
SELECT yb_get_range_split_clause('tbl10_idx'::regclass);
DROP INDEX tbl10_idx;
DROP TABLE tbl10;

CREATE TABLE tbl11 (a TEXT, b INT, c INT, d INT);
CREATE UNIQUE INDEX tbl11_idx ON tbl11 (
  a ASC, b ASC, c ASC, d ASC
) SPLIT AT VALUES (('bar', 1, 1, 1), ('foo', 2, 2, 2), ('qux', 3, 3, 3));
SELECT yb_get_range_split_clause('tbl11_idx'::regclass);
DROP INDEX tbl11_idx;
DROP TABLE tbl11;

-- Test GIN index
CREATE TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE INDEX vectors_idx ON vectors USING ybgin (v) SPLIT AT VALUES (
  (MINVALUE), ('ccc'), ('fff'), (MAXVALUE));
SELECT yb_get_range_split_clause('vectors_idx'::regclass);
DROP INDEX vectors_idx;
DROP TABLE vectors;

CREATE TABLE int_arrays (i serial PRIMARY KEY, a INT[]);
CREATE INDEX int_arrays_idx ON int_arrays USING ybgin (a) SPLIT AT VALUES (
  (MINVALUE), (2), (4), (6), (MAXVALUE));
SELECT yb_get_range_split_clause('int_arrays_idx'::regclass);
DROP INDEX int_arrays_idx;
DROP TABLE int_arrays;

CREATE TABLE text_arrays (i serial PRIMARY KEY, a TEXT[]);
CREATE INDEX text_arrays_idx ON text_arrays USING ybgin (a) SPLIT AT VALUES (
  (MINVALUE), ('aa'), ('bb'), ('zz'), (MAXVALUE));
SELECT yb_get_range_split_clause('text_arrays_idx'::regclass);
DROP INDEX text_arrays_idx;
DROP TABLE text_arrays;

-- Test serial
CREATE TABLE tbl_serial_pk (
 a SERIAL,
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES((-100), (200));
SELECT yb_get_range_split_clause('tbl_serial_pk'::regclass);
DROP TABLE tbl_serial_pk;

-- Test collation
CREATE TABLE tbl_collation_pk (
 a TEXT COLLATE "C",
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES(('bar'), ('foo'));
SELECT yb_get_range_split_clause('tbl_collation_pk'::regclass);
DROP TABLE tbl_collation_pk;

CREATE TABLE tbl_collation_pk2 (
 a TEXT COLLATE "en-x-icu",
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES(('äbc'), ('bbc'));
SELECT yb_get_range_split_clause('tbl_collation_pk2'::regclass);
DROP TABLE tbl_collation_pk2;

CREATE TABLE tbl_collation_pk3 (
 a TEXT COLLATE "C",
 PRIMARY KEY(a ASC)
) SPLIT AT VALUES(('bbc'), ('äbc'));
SELECT yb_get_range_split_clause('tbl_collation_pk3'::regclass);
DROP TABLE tbl_collation_pk3;

-- Test colocated table
CREATE DATABASE colocation_test colocation = true;
\c colocation_test
CREATE TABLE tbl_colocated_range (
  a INT,
  b TEXT,
  PRIMARY KEY(a ASC, b ASC)
) SPLIT AT VALUES((-100, 'bar'), (250, 'foo'));

CREATE TABLE tbl_colocated_hash (
  a INT PRIMARY KEY
) SPLIT INTO 2 TABLETS;

CREATE TABLE tbl_colocated (
  a INT,
  b TEXT,
  PRIMARY KEY(a ASC, b ASC)
);
SELECT yb_get_range_split_clause('tbl_colocated'::regclass);

\c yugabyte
DROP DATABASE colocation_test;

-- Test tablegroup
CREATE TABLEGROUP tbl_group;

CREATE TABLE tbl_group_split_at (
  a INT,
  PRIMARY KEY(a ASC)
) SPLIT AT VALUES((-100), (250)) TABLEGROUP tbl_group;

CREATE TABLE tbl_group_table (
  a INT,
  PRIMARY KEY(a ASC)
) TABLEGROUP tbl_group;
SELECT yb_get_range_split_clause('tbl_group_table'::regclass);
DROP TABLE tbl_group_table;

DROP TABLEGROUP tbl_group;

-- Test the scenario where primary key columns' ordering in PRIMARY KEY
-- constraint is different from the key column's ordering in CREATE TABLE
-- statement.
CREATE TABLE column_ordering_mismatch (
  k2 TEXT,
  v DOUBLE PRECISION,
  k1 INT,
  PRIMARY KEY (k1 ASC, k2 ASC)
) SPLIT AT VALUES((1, '1'), (100, '100'));
SELECT yb_get_range_split_clause('column_ordering_mismatch'::regclass);
DROP TABLE column_ordering_mismatch;

-- Test table PRIMARY KEY with INCLUDE clause
CREATE TABLE tbl_with_include_clause (
  k2 TEXT,
  v DOUBLE PRECISION,
  k1 INT,
  PRIMARY KEY (k1 ASC, k2 ASC) INCLUDE (v)
) SPLIT AT VALUES((1, '1'), (100, '100'));
SELECT yb_get_range_split_clause('tbl_with_include_clause'::regclass);
DROP TABLE tbl_with_include_clause;

-- Test index SPLIT AT with INCLUDE clause
CREATE TABLE test_tbl (
  a TEXT,
  b DOUBLE PRECISION,
  PRIMARY KEY (a ASC)
) SPLIT AT VALUES(('11'));
CREATE INDEX test_idx on test_tbl(
  b ASC
) INCLUDE (a) SPLIT AT VALUES ((1.1));
SELECT yb_get_range_split_clause('test_idx'::regclass);
DROP INDEX test_idx;
DROP TABLE test_tbl;

-- Test index SPLIT AT with INCLUDE clause
CREATE TABLE test_tbl (
  a INT,
  b TEXT,
  c CHAR,
  d BOOLEAN,
  e REAL,
  PRIMARY KEY (a ASC, b ASC)
) SPLIT AT VALUES((1, '111'));
CREATE INDEX test_idx on test_tbl(
  a ASC,
  b ASC,
  c ASC
) INCLUDE (d, e) SPLIT AT VALUES ((1, '11', '1'));
SELECT yb_get_range_split_clause('test_idx'::regclass);
DROP INDEX test_idx;
DROP TABLE test_tbl;

-- Test index SPLIT AT with INCLUDE clause
CREATE TABLE test_tbl (
  a INT,
  b INT,
  c INT,
  d INT,
  e INT,
  PRIMARY KEY (a DESC, b ASC)
) SPLIT AT VALUES((1, 1));
CREATE INDEX test_idx on test_tbl(
  a ASC,
  b DESC
) INCLUDE (c, d, e) SPLIT AT VALUES ((1, 1));
SELECT yb_get_range_split_clause('test_idx'::regclass);
DROP INDEX test_idx;
DROP TABLE test_tbl;

-- Test secondary index with duplicate columns and backwards order columns
CREATE TABLE test_tbl (
  k1 INT,
  k2 TEXT,
  k3 DOUBLE PRECISION
);
CREATE INDEX test_idx on test_tbl (
  k3 ASC,
  k2 ASC,
  k1 ASC,
  k3 DESC
) SPLIT AT VALUES ((1.1, '11', 1, 1.1), (3.3, '33', 3, 3.3));
SELECT yb_get_range_split_clause('test_idx'::regclass);
DROP INDEX test_idx;
DROP TABLE test_tbl;
