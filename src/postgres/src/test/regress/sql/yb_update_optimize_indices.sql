SET yb_fetch_row_limit TO 1024;
SET yb_explain_hide_non_deterministic_fields TO true;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

-- This test requires the t-server gflag 'ysql_skip_row_lock_for_update' to be set to false.

-- CREATE a table with a primary key and no secondary indexes
DROP TABLE IF EXISTS pkey_only_table;
CREATE TABLE pkey_only_table (h INT PRIMARY KEY, v1 INT, v2 INT);
INSERT INTO pkey_only_table (SELECT i, i, i FROM generate_series(1, 10240) AS i);
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM pkey_only_table;

-- Updating non-index column should be done in a single op
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 1, v2 = 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 2 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 3, v2 = 3 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = v1 + 1, v2 = v1 + 1 WHERE h = 1;

-- Setting index column in the update should trigger a read, but no update of the index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 1, h = 1 WHERE h = 1;
-- TODO: Fix bug here
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 1, h = h WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v1 = 1, h = v1 WHERE h = 1;

-- CREATE a table with a multi-column secondary index
DROP TABLE IF EXISTS secindex_only_table;
CREATE TABLE secindex_only_table (h INT, v1 INT, v2 INT, v3 INT);
CREATE INDEX NONCONCURRENTLY secindex_only_table_v1_v2 ON secindex_only_table((v1, v2) HASH);
INSERT INTO secindex_only_table (SELECT i, i, i, i FROM generate_series(1, 10240) AS i);

-- Setting the secondary index columns in the should trigger a read, but no update of the index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = h WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = v1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = v1 + v2 - h WHERE h = 1;

-- Same cases as above, but not providing the primary key
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = h WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = v1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = h, h = v1 + v2 - h WHERE v1 = 1;

-- Queries with non-leading secondary index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET h = h WHERE h = 1;
--

-- CREATE a table having secondary indices with NULL values
DROP TABLE IF EXISTS nullable_table;
CREATE TABLE nullable_table (h INT PRIMARY KEY, v1 INT, v2 INT, v3 INT);
CREATE INDEX NONCONCURRENTLY nullable_table_v1_v2 ON nullable_table ((v1, v2) HASH);
INSERT INTO nullable_table (SELECT i, NULL, NULL, NULL FROM generate_series(1, 100) AS i);

EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET v1 = NULL WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET v1 = NULL, v2 = NULL WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET h = 1, v1 = NULL, v2 = NULL WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET h = h, v1 = NULL, v2 = NULL WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET h = h, v1 = NULL, v2 = NULL WHERE v1 = NULL;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET h = h, v1 = NULL, v2 = NULL WHERE v1 = NULL OR v2 = NULL;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE nullable_table SET h = h, v1 = NULL, v2 = NULL WHERE v1 = NULL AND v2 = NULL;

-- CREATE a table having secondary indices in a colocated database.


-- CREATE a table having indexes of multiple data types
DROP TABLE IF EXISTS number_type_table;
CREATE TABLE number_type_table (h INT PRIMARY KEY, v1 INT2, v2 INT4, v3 INT8, v4 BIGINT, v5 FLOAT4, v6 FLOAT8, v7 SERIAL, v8 BIGSERIAL, v9 NUMERIC(10, 4), v10 NUMERIC(100, 40));
CREATE INDEX NONCONCURRENTLY number_type_table_v1_v2 ON number_type_table (v1 ASC, v2 DESC);
CREATE INDEX NONCONCURRENTLY number_type_table_v3 ON number_type_table (v3 HASH);
CREATE INDEX NONCONCURRENTLY number_type_table_v4_v5_v6 ON number_type_table ((v4, v5) HASH, v6 ASC);
CREATE INDEX NONCONCURRENTLY number_type_table_v7_v8 ON number_type_table (v7 HASH, v8 DESC);
CREATE INDEX NONCONCURRENTLY number_type_table_v9_v10 ON number_type_table (v9 HASH) INCLUDE (v10, v1);

INSERT INTO number_type_table(h, v1, v2, v3, v4, v5, v6, v9, v10) VALUES (0, 1, 2, 3, 4, 5.0, 6.000001, '-9.1234', '-10.123455789');
INSERT INTO number_type_table(h, v1, v2, v3, v4, v5, v6, v9, v10) VALUES (10, 11, 12, 13, 14, 15.01, 16.000002, '-19.1234', '-20.123455789');

EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_type_table SET h = 0, v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5, v6 = 6.000001, v7 = 1, v8 = 1, v9 = '-9.12344', v10 = '-10.123455789' WHERE h = 0;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_type_table SET h = h + 1, v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5, v6 = 6.000001, v7 = 1, v8 = 1, v9 = '-9.12344', v10 = '-10.123455789' WHERE h = 0;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_type_table SET h = h + 1, v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5, v6 = 6.000001, v7 = 1, v8 = 1, v9 = '-9.12344', v10 = '-10.123455789';

CREATE TYPE mood_type AS ENUM ('sad', 'ok', 'happy');
DROP TABLE IF EXISTS non_number_type_table;
CREATE TABLE non_number_type_table(tscol TIMESTAMP PRIMARY KEY, varcharcol VARCHAR(8), charcol CHAR(8), textcol TEXT, linecol LINE, ipcol CIDR, uuidcol UUID, enumcol mood_type);
CREATE INDEX NONCONCURRENTLY non_number_type_table_mixed ON non_number_type_table(tscol, varcharcol);
CREATE INDEX NONCONCURRENTLY non_number_type_table_text ON non_number_type_table((varcharcol, charcol) HASH, textcol ASC);
CREATE INDEX NONCONCURRENTLY non_number_type_table_text_hash ON non_number_type_table(textcol HASH);
-- This should fail as indexes on geometric and network types are not yet supported
CREATE INDEX NONCONCURRENTLY non_number_type_table_text_geom_ip ON non_number_type_table(linecol ASC, ipcol DESC);
CREATE INDEX NONCONCURRENTLY non_number_type_table_uuid_enum ON non_number_type_table(uuidcol ASC, enumcol DESC);

INSERT INTO non_number_type_table VALUES('1999-01-08 04:05:06 -8:00', 'varchar1', 'charpad', 'I am batman', '{1, 2, 3}'::line, '1.2.3.0/24'::cidr, 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'::uuid, 'happy'::mood_type);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE non_number_type_table SET tscol = 'January 8 04:05:06 1999 PST', varcharcol = 'varchar1', charcol = 'charpad', textcol = 'I am not batman :(', linecol = '{1, 2, 3}'::line, ipcol = '1.2.3'::cidr, uuidcol = 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'::uuid, enumcol = 'happy' WHERE tscol = 'January 8 07:05:06 1999 EST';

DROP TABLE pkey_only_table;
DROP TABLE secindex_only_table;
DROP TABLE nullable_table;
DROP TABLE non_number_type_table;
DROP TABLE number_type_table;

---
--- Test to validate behavior of equality comparisons across data types
---
-- Test for json data types
CREATE TABLE json_types_table (h INT PRIMARY KEY, v1 INT, v2 JSONB, v3 JSON);
CREATE INDEX NONCONCURRENTLY ON json_types_table (v1) INCLUDE (v2, v3);

INSERT INTO json_types_table VALUES (1, 1, '{"a": 1, "b": 2}'::jsonb, '{"a": 1, "b": 2}'::json);
INSERT INTO json_types_table VALUES (2, 2, '{"b": 2, "a": 1}', '{"b": 2, "a": 1}');

-- Equality comparisons on data type with no equality operator (json) should succeed
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1 WHERE h = 1;
-- Two different representations of the same data type should be considered equal
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{"b": 2, "a": 1}'::jsonb WHERE h = 1;
-- JSONB is normalized, while JSON is not, causing an index update
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v3 = '{"b": 2, "a": 1}'::json WHERE h = 1;
-- One with white spaces
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{    "b" : 2   ,     "a" :  1}'::jsonb WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{"b":2,"a":1}'::json WHERE h = 1; , 
-- Casting via not specifying type
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{"b": 2, "a": 1}', v3 = '{"a": 1, "b": 1}' WHERE h = 2;
-- Casting via specifying input as a castable type
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{"b": 2, "a": 1}'::json, v3 = '{"a": 1, "b": 2}'::jsonb WHERE h = 2;
-- Casting an un-normalized JSONB to JSON should cause an index update as the input will never be normalized
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE json_types_table SET v1 = 1, v2 = '{"b": 2, "a": 1}'::json, v3 = '{"a": 1, "b": 2}'::jsonb WHERE h = 2;

-- TODO(kramanathan): PG 15 has jsonpath support. Add tests for jsonpath

-- Tests for geometric data types
CREATE TABLE geometric_types_table (h INT PRIMARY KEY, v1 INT, v2 POINT, v3 LINE, v4 LSEG, v5 BOX, v6 PATH, v7 POLYGON, v8 CIRCLE);
CREATE INDEX NONCONCURRENTLY ON geometric_types_table (v1) INCLUDE (v2, v3, v4, v5, v6, v7, v8);
INSERT INTO geometric_types_table VALUES (1, 1, '(3.0, 4.0)'::point, '[(1.0, 2.0), (3.0, 4.0)]'::line, '[(1, 2), (3, 4)]'::lseg, '(1, 2), (3, 4)'::box, '[(1, 2), (3, 4)]'::path, '((1, 2), (3, 4), (5, 6))'::polygon, '<(1, 2), 3>'::circle);

-- Convert point and line from float representation to int representation (implicit casting)
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v2 = '(3, 4)'::point, v3 = '[(1.0, 2), (3, 4.0)]'::line WHERE h = 1;
-- Individual coordinates are stored as float8. Test cases where input has higher precision
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v2 = '(2.999999999999999999, 3.999999999999999999)'::point, v3 = '[(0.999999999999999999, 2.00), (2.999999999999999999, 4)]'::line WHERE h = 1;
-- Alternate line and line segment representations; note that line segment is directed
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '[(3, 4), (1, 2)]'::line, v4 = '[(1, 2), (3, 4)]'::lseg WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '((3, 4), (1, 2))'::line, v4 = '((1, 2), (3, 4))'::lseg WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '(3, 4), (1, 2)'::line, v4 = '(1, 2), (3, 4)'::lseg WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '3, 4, 1, 2'::line, v4 = '1, 2, 3, 4'::lseg WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '[(3, 4), (1, 2)]'::line, v4 = '[(1, 2), (3, 4)]'::lseg WHERE h = 1;
-- Line is equivalent to x - y + 1 = 0 => A = 1, B = -1, C = 1
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '{1, -1, 1}'::line WHERE h = 1;
-- A line can be represented by other points that lie outside the boundary of the supplied points
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v3 = '[(5, 6), (7, 8)]'::line WHERE h = 1;
-- Alternate box representations
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v5 = '(3, 2), (1, 4)'::box WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v5 = '((3, 2), (1, 4))'::box WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v5 = '3, 2, 1, 4'::box WHERE h = 1;
-- Alterate representations of a circle
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v8 = '(1, 2), 3'::circle WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v8 = '((1, 2), 3)'::circle WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE geometric_types_table SET v1 = 1, v8 = '1, 2, 3'::circle WHERE h = 1;

-- Test for array data types
CREATE TABLE array_types_table (h INT PRIMARY KEY, v1 INT, v2 INT4[], v3 FLOAT4[], v4 TEXT[], v5 JSONB[], v6 BYTEA[]);
CREATE INDEX NONCONCURRENTLY ON array_types_table (v1) INCLUDE (v2, v3, v4, v5, v6);
INSERT INTO array_types_table VALUES (1, 1, ARRAY[1, 2, 3], ARRAY[1.0, 2.0, 3.0], ARRAY['a', 'b', 'c'], ARRAY['{"a": 1, "b": 2}', '{"c": 3, "d": 4}']::jsonb[], ARRAY['abc'::bytea, 'def'::bytea, 'ghi'::bytea]);

-- Replace the entire array
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v2 = '{1, 2, 3}', v3 = '{1, 2, 3}', v4 = '{"a", "b", "c"}' WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v2 = ARRAY[1, 2, 3], v3 = ARRAY[1, 2, 3], v4 = ARRAY['a', 'b', 'c'] WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v5 = ARRAY['{"a": 1, "b": 2}', '{"c": 3, "d": 4}']::jsonb[], v6 = ARRAY['abc', 'def', 'ghi']::bytea[] WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v6 = ARRAY['\x616263'::bytea, '\x646566'::bytea, '\x676869'::bytea] WHERE h = 1;
-- Replace specific elements in the array
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v2[2] = 2, v3[2] = 1.999999999999999999, v4[3] = 'c' WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v6[1] = '\x616263'::bytea WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE array_types_table SET v1 = 1, v2[2:3] = '{2, 3}', v3[1:2] = '{0.999999999999999999, 1.999999999999999999}', v4[1:3] = ARRAY['a', 'b', 'c'] WHERE h = 1;

-- Test for type modifiers
-- REAL has 6 decimal digits of precision, NUMERIC has type modifiers 'precision' and 'scale'.
-- Precision is the total number of digits, scale is the number of digits after the decimal point.
CREATE TABLE numeric_table (h INT PRIMARY KEY, v1 REAL, v2 NUMERIC(4, 2), v3 NUMERIC(10, 4), v4 FLOAT4);
CREATE INDEX NONCONCURRENTLY ON numeric_table (v1) INCLUDE (v2, v3, v4);
INSERT INTO numeric_table VALUES (1, 1.234567, 12.34, 1.234567, 1.23456789012);
SELECT * FROM numeric_table;

-- Query to check that the storage representation remains identical after type modifiers have been applied.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = 1.2345670, v2 = 12.344, v4 = 1.23456789567 WHERE h = 1;
-- Query to check that casting from a wider type to a narrower type results in "rounding" behavior.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = 1.2345670, v2 = 12.336, v3 = 1.234650 WHERE h = 1;
-- Casting from infinity to infinity should not cause a change in representation.
-- Note that NUMERIC does not support infinity prior to PG 14.
INSERT INTO numeric_table VALUES (2, 'inf', 'Infinity', '-Infinity', '-inf');
INSERT INTO numeric_table VALUES (2, 'inf', 0, 0, '-inf');
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = 'inf'::real + 1, v4 = '-Infinity'::float4 + 1 WHERE h = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = v1 + 'inf'::real + 1, v4 = v4 + '-Infinity'::float4 + 1 WHERE h = 2;
-- Casting from infinity to NaN should however cause a change in representation.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = v1 - 'inf'::real, v4 = v4 - '-Infinity'::float4 WHERE h = 2;
SELECT * FROM numeric_table;
-- Casting from NaN to NaN should not cause a change in representation.
-- Similarly, note that NaN is not supported for NUMERIC types in Yugabyte (GH-711) does not support NaN prior to PG 14.
-- This should error out
INSERT INTO numeric_table VALUES (3, 'nan', 'NaN', 'naN', 'NaN');
INSERT INTO numeric_table VALUES (3, 'nan', 0, 0, 'NaN');
-- ('inf'::real - 'inf'::real) produces a different representation of NaN than 'NaN'::real in some
-- compilers/optimization levels. So the query below updates the index in some cases, but not in others.
-- Commenting it out to avoid test failures.
-- EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = 'inf'::real - 'inf'::real, v4 = v4 - '-Infinity'::float4 - '+Inf'::float4 WHERE h = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE numeric_table SET v1 = v1 - 'inf'::real, v4 = v4 - '-Infinity'::float4 - '+Inf'::float4 WHERE h = 3;
SELECT * FROM numeric_table;

-- Test for casting between data types
CREATE TABLE number_table (h INT PRIMARY KEY, v1 INT, v2 INT4, v3 INT8, v4 FLOAT4, v5 FLOAT8);
CREATE INDEX NONCONCURRENTLY ON number_table (v1) INCLUDE (v2, v3, v4, v5);
INSERT INTO number_table VALUES (1, 1, 1, 1, 1.0, 1.0);

-- Casting from a larger type to a smaller type should either retain the same representation
-- if the value is identical or produce an overflow.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v2 = (2147483647 + 1)::int8 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v4 = (3.4e39)::float8 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v2 = (1)::int8, v4 = (1.0)::float8 WHERE h = 1;

-- Casting from a smaller to a larger type should either retain the same representation when the
-- value is identical.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v3 = (1)::int4, v5 = (1.0)::float4 WHERE h = 1;

-- Casting between types should not cause an index update if the value remains the same.
INSERT INTO number_table VALUES (2, 2, 17, 15, 17.25, 15.75);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v2 = (17.0)::float4, v3 = (15.0)::float8 WHERE h = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v2 = 17.0, v3 = 15.0 WHERE h = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v4 = 17::int4 + 0.25::float(8), v5 = 15::int8 + 0.75::float(4) WHERE h = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE number_table SET v4 = 17 + 0.25, v5 = 15 + 0.75 WHERE h = 2;

-- Test for padding in text data types.
CREATE TABLE text_table (h INT PRIMARY KEY, v1 INT, v2 TEXT, v3 CHAR(8), v4 VARCHAR(8));
CREATE INDEX NONCONCURRENTLY ON text_table (v1) INCLUDE (v2, v3, v4);
INSERT INTO text_table VALUES (1, 1, 'some-text', 'abcdef', 'abcde');

-- The following queries should not cause an index update as the text type's value remains unmodified
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE text_table SET v2 = 'some-text', v3 = 'abcdef', v4 = 'abcde' WHERE h = 1;
-- Postgres does not support NULL characters in strings. The padding does not end up adding any characters.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE text_table SET v2 = rpad('some-text'::char(12), 12, ''), v3 = rpad('abcdef'::char(8), 8, ''), v4 = rpad('abcde'::char(8), 8, '') WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE text_table SET v2 = lpad('some-text'::text, 12, ''), v3 = lpad('abcdef'::text, 8, ''), v4 = lpad('abcde'::text, 8, '') WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE text_table SET v2 = ''::char(4) || 'some-text' || ''::char(4), v3 = 'abcdef' || ''::char(2), v4 = 'abcde' || ''::char(3) WHERE h = 1;

-- The following query ends up updating the index because postgres truncates the trailing NULL characters.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE text_table SET v4 = 'abcde'::char(8) || '000' WHERE h = 1;
SELECT * FROM text_table;

-- Test for composite data types
CREATE TYPE complex1 AS (v1 INT4, v2 FLOAT4, v3 TEXT, v4 JSONB, v5 BYTEA);
CREATE TABLE composite_table (h INT PRIMARY KEY, v INT, c1 complex1);
CREATE INDEX NONCONCURRENTLY ON composite_table (v) INCLUDE (c1);

INSERT INTO composite_table VALUES (1, 1, ROW(100, 123.45, 'some-text', '{"a": 1, "b": 2}'::jsonb, '{"c": 3, "d": 4}'::bytea));

-- The following queries should not cause an index update as the composite type's value remains unmodified
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1.v1 = 100 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET v = 1, c1.v3 = 'some-text' WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1.v2 = 123.45, c1.v5 = '{"c": 3, "d": 4}'::bytea, c1.v4 = '{"a": 1, "b": 2}'::jsonb WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1 = ROW(100, 123.45, 'some-text', '{"a": 1, "b": 2}'::jsonb, '{"c": 3, "d": 4}'::bytea) WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1 = (100, 123.45, 'some-text', '{"a": 1, "b": 2}'::jsonb, '{"c": 3, "d": 4}'::bytea) WHERE h = 1;
-- Queries with implicit casting
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1 = (100, 123.45, 'some-text', '{"a": 1, "b": 2}', '{"c": 3, "d": 4}') WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1 = (100.40, 123.45, 'some-text', '{"b": 2, "a": 1}', '{"c": 3, "d": 4}') WHERE h = 1;

SELECT * FROM composite_table;

-- The following queries should produce an index update as the composite type's value is modified
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1.v1 = (c1).v2 + 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET v = v + 1, c1.v3 = 'some-text' WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET v = v + 1, c1.v3 = 'someother-text' WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1.v2 = 123.451, c1.v5 = '{"d": 4, "c": 3}'::bytea, c1.v4 = '{"a": 2, "b": 1}'::jsonb WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE composite_table SET c1 = ROW((v) + 100, (c1).v1 + 123.45, 'someother-text', '{"a": 2, "b": 1}'::jsonb, '{"d": 4, "c": 3}'::bytea) WHERE h = 1;

SELECT * FROM composite_table;

DROP TABLE composite_table;
DROP TABLE text_table;
DROP TABLE number_table;
DROP TABLE numeric_table;
DROP TABLE array_types_table;
DROP TABLE geometric_types_table;
DROP TABLE json_types_table;
