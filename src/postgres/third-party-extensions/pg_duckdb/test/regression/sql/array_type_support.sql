-- INT4 (zero dimension)
CREATE TABLE int_array_0d(a INT[]);
INSERT INTO int_array_0d SELECT CAST(a as INT[]) FROM (VALUES
    ('{}')
) t(a);
SELECT * FROM int_array_0d;

-- INT4 (single dimension)
CREATE TABLE int_array_1d(a INT[]);
INSERT INTO int_array_1d SELECT CAST(a as INT[]) FROM (VALUES
    ('{1, 2, 3}'),
    (NULL),
    ('{4, 5, NULL, 7}'),
    ('{}')
) t(a);
SELECT * FROM int_array_1d;

SET duckdb.log_pg_explain = true;
SELECT * FROM int_array_1d WHERE a @> ARRAY[4];

SELECT * FROM duckdb.query($$ FROM pgduckdb.public.int_array_1d WHERE contains(a, 4) $$);
RESET duckdb.log_pg_explain;

-- INT4 (two dimensional data, single dimension type)
CREATE TABLE int_array_2d(a INT[]);
INSERT INTO int_array_2d VALUES
    ('{{1, 2}, {3, 4}}'),
    ('{{5, 6, 7}, {8, 9, 10}}'),
    ('{{11, 12, 13}, {14, 15, 16}}'),
    ('{{17, 18}, {19, 20}}');
SELECT * FROM int_array_2d;
drop table int_array_2d;

-- INT4 (single dimensional data, two dimensionsal type)
CREATE TABLE int_array_2d(a INT[][]);
INSERT INTO int_array_2d VALUES
    ('{1, 2}'),
    ('{5, 6, 7}'),
    ('{11, 12, 13}'),
    ('{17, 18}');
SELECT * FROM int_array_2d;
drop table int_array_2d;

-- INT4 (two dimensional data and type)
CREATE TABLE int_array_2d(a INT[][]);
INSERT INTO int_array_2d VALUES
    ('{{1, 2}, {3, 4}}'),
    ('{{5, 6, 7}, {8, 9, 10}}'),
    ('{{11, 12, 13}, {14, 15, 16}}'),
    ('{{17, 18}, {19, 20}}');
SELECT * FROM int_array_2d;

-- INT8 (single dimension)
CREATE TABLE bigint_array_1d(a BIGINT[]);
INSERT INTO bigint_array_1d SELECT CAST(a as BIGINT[]) FROM (VALUES
    ('{9223372036854775807, 2, -9223372036854775808}'),
    (NULL),
    ('{4, 4294967296, NULL, 7}'),
    ('{}')
) t(a);
SELECT * FROM bigint_array_1d;

-- BOOL (single dimension)
CREATE TABLE bool_array_1d(a BOOL[]);
INSERT INTO bool_array_1d SELECT CAST(a as BOOL[]) FROM (VALUES
    ('{true, false, true}'),
    (NULL),
    ('{true, true, NULL, false}'),
    ('{}')
) t(a);
SELECT * FROM bool_array_1d;

-- CHAR (single dimension)
CREATE TABLE char_array_1d(a CHAR[]);
INSERT INTO char_array_1d SELECT CAST(a as CHAR[]) FROM (VALUES
    ('{a,b,c}'),
    (NULL),
    ('{d,e,NULL,g}'),
    ('{}')
) t(a);
SELECT * FROM char_array_1d;

-- SMALLINT (single dimension)
CREATE TABLE smallint_array_1d(a SMALLINT[]);
INSERT INTO smallint_array_1d SELECT CAST(a as SMALLINT[]) FROM (VALUES
    ('{32767, -32768, 0}'),
    (NULL),
    ('{1, 2, NULL, 3}'),
    ('{}')
) t(a);
SELECT * FROM smallint_array_1d;

-- VARCHAR (single dimension)
CREATE TABLE varchar_array_1d(a VARCHAR[]);
INSERT INTO varchar_array_1d SELECT CAST(a as VARCHAR[]) FROM (VALUES
    ('{hello,world}'),
    (NULL),
    ('{test,NULL,array}'),
    ('{}')
) t(a);
SELECT * FROM varchar_array_1d;

-- VARBIT (single dimension)
CREATE TABLE varbit_array_1d(a VARBIT[]);
INSERT INTO varbit_array_1d SELECT CAST(a as VARBIT[]) FROM (VALUES
    ('{B1010, B10100011}'),
    (NULL),
    ('{B1010001011, NULL, B10101010101}'),
    ('{}')
) t(a);
SELECT * FROM varbit_array_1d;

-- BIT (single dimension)
CREATE TABLE bit_array_1d(a BIT(4)[]);
INSERT INTO bit_array_1d SELECT CAST(a as BIT(4)[]) FROM (VALUES
    ('{B1010, B0101}'),
    (NULL),
    ('{B1010, NULL, B0111}'),
    ('{}')
) t(a);
SELECT * FROM bit_array_1d;

-- INTERVAL (single dimension)
CREATE TABLE interval_array_1d(a INTERVAL[]);
INSERT INTO interval_array_1d (a) VALUES (ARRAY['2 years 5 months 1 day 3 hours 30 minutes 5 seconds', '5 days 5 hours']::INTERVAL[]);
INSERT INTO interval_array_1d (a) VALUES (ARRAY['3 seconds']::INTERVAL[]);
INSERT INTO interval_array_1d (a) VALUES (ARRAY[NULL]::INTERVAL[]);
SELECT * FROM interval_array_1d;

-- TIME (single dimension)
CREATE TABLE time_array_1d(a TIME[]);
INSERT INTO time_array_1d (a) VALUES (ARRAY[MAKE_TIME(8, 30, 0), MAKE_TIME(12, 30, 0)]::TIME[]);
INSERT INTO time_array_1d (a) VALUES (ARRAY[MAKE_TIME(23, 59, 59)]::TIME[]);
INSERT INTO time_array_1d (a) VALUES (ARRAY[NULL::TIME]::TIME[]);
SELECT * FROM time_array_1d;

-- TIMETZ (single dimension)
CREATE TABLE timetz_array_1d(a TIMETZ[]);
INSERT INTO timetz_array_1d (a) VALUES (ARRAY['08:30:00+05'::TIMETZ,'12:30:00-05'::TIMETZ]::TIMETZ[]);
INSERT INTO timetz_array_1d (a) VALUES (ARRAY['23:59:59+00'::TIMETZ]::TIMETZ[]);
INSERT INTO timetz_array_1d (a) VALUES (ARRAY[NULL::TIMETZ]::TIMETZ[]);
SELECT * FROM timetz_array_1d;

-- TIMESTAMP (single dimension)
CREATE TABLE timestamp_array_1d(a TIMESTAMP[]);
INSERT INTO timestamp_array_1d SELECT CAST(a as TIMESTAMP[]) FROM (VALUES
    ('{2023-01-01 12:00:00, 2023-01-02 13:30:00}'),
    (NULL),
    ('{2023-01-03 09:15:00, NULL, 2023-01-04 18:45:00}'),
    ('{}')
) t(a);
SELECT * FROM timestamp_array_1d;

-- FLOAT4 (single dimension)
CREATE TABLE float4_array_1d(a FLOAT4[]);
INSERT INTO float4_array_1d SELECT CAST(a as FLOAT4[]) FROM (VALUES
    ('{1.1, 2.2, 3.3}'),
    (NULL),
    ('{4.4, 5.5, NULL, 7.7}'),
    ('{}')
) t(a);
SELECT * FROM float4_array_1d;

-- FLOAT8 (single dimension)
CREATE TABLE float8_array_1d(a FLOAT8[]);
INSERT INTO float8_array_1d SELECT CAST(a as FLOAT8[]) FROM (VALUES
    ('{1.11111, 2.22222, 3.33333}'),
    (NULL),
    ('{4.44444, 5.55555, NULL, 7.77777}'),
    ('{}')
) t(a);
SELECT * FROM float8_array_1d;

-- NUMERIC (single dimension)
CREATE TABLE numeric_array_1d(a NUMERIC[]);
INSERT INTO numeric_array_1d SELECT CAST(a as NUMERIC[]) FROM (VALUES
    ('{1.1, 2.2, 3.3}'),
    (NULL),
    ('{4.4, 5.5, NULL, 7.7}'),
    ('{}')
) t(a);
SELECT * FROM numeric_array_1d;
SET duckdb.convert_unsupported_numeric_to_double = true;
SELECT * FROM numeric_array_1d;
RESET duckdb.convert_unsupported_numeric_to_double;

-- UUID (single dimension)
CREATE TABLE uuid_array_1d(a UUID[]);
INSERT INTO uuid_array_1d SELECT CAST(a as UUID[]) FROM (VALUES
    ('{a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11, a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a12}'),
    (NULL),
    ('{a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a13, NULL, a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a14}'),
    ('{}')
) t(a);
SELECT * FROM uuid_array_1d;

-- JSON (single dimension)
CREATE TABLE json_array_1d(a JSON[]);
INSERT INTO json_array_1d VALUES
    (ARRAY['{"key": "value"}', '{"array": [1, 2, 3]}']::JSON[]),
    (NULL),
    (ARRAY['{"object": {"nested": "value"}}', NULL, '{"number": 42}']::JSON[]),
    (ARRAY[]::JSON[]);
SELECT * FROM json_array_1d;


-- JSONB (single dimension)
CREATE TABLE jsonb_array_1d(a JSONB[]);
INSERT INTO jsonb_array_1d VALUES
    (ARRAY['{"key": "value"}', '{"array": [1, 2, 3]}']::JSONB[]),
    (ARRAY[
        '{"key1": "value1"}'::jsonb,
        '{"key2": "value2"}'::jsonb
    ]),
    (NULL),
    (ARRAY['{"object": {"nested": "value"}}', NULL, '{"number": 42}']::JSONB[]),
    (ARRAY[]::JSONB[]);
SELECT * FROM jsonb_array_1d;

-- REGCLASS (single dimension)
CREATE TABLE regclass_array_1d(a REGCLASS[]);
INSERT INTO regclass_array_1d VALUES
    ('{pg_class, pg_attribute}'),
    (NULL),
    ('{pg_type, NULL, pg_index}'),
    ('{}');
SELECT * FROM regclass_array_1d;




-- CHAR (two dimensions)
CREATE TABLE char_array_2d(a CHAR(1)[][]);
INSERT INTO char_array_2d VALUES
    ('{{"a","b"},{"c","d"}}'),
    ('{{"e","f","g"},{"h","i","j"}}'),
    (NULL),
    ('{{"k","l"},{"m",NULL}}'),
    ('{}');
SELECT * FROM char_array_2d;

-- SMALLINT (two dimensions)
CREATE TABLE smallint_array_2d(a SMALLINT[][]);
INSERT INTO smallint_array_2d VALUES
    ('{{1,2},{3,4}}'),
    ('{{5,6,7},{8,9,10}}'),
    (NULL),
    ('{}'),
    ('{{11,12},{NULL,14}}');
SELECT * FROM smallint_array_2d;

-- VARCHAR (two dimensions)
CREATE TABLE varchar_array_2d(a VARCHAR[][]);
INSERT INTO varchar_array_2d VALUES
    ('{{"hello","world"},{"foo","bar"}}'),
    ('{{"test","array","data"},{"more","text","here"}}'),
    (NULL),
    ('{}'),
    ('{{"some","strings"},{NULL,"last"}}');
SELECT * FROM varchar_array_2d;

-- VARBIT (two dimensions)
CREATE TABLE varbit_array_2d(a VARBIT[][]);
INSERT INTO varbit_array_2d VALUES
    ('{{B1010,B10100011},{B1010101,B101010101}}'),
    ('{{B101010101,B10101010101,B1010101010101},{B101010101010101,B10101010101010101,B1010101010101010101}}'),
    (NULL),
    ('{}'),
    ('{{B101010101,B10101010101},{NULL,B1010101010101}}');
SELECT * FROM varbit_array_2d;

CREATE TABLE bit_array_2d(a BIT(4)[][]);
INSERT INTO bit_array_2d SELECT CAST(a as BIT(4)[][]) FROM (VALUES
    ('{{B1010, B0101},{B0000, B0111}}'),
    ('{{B1010, B0101, B1111},{B1010, B0101, B0000}}'),
    (NULL),
    ('{}'),
    ('{{B1010, NULL},{B0111, B0000}}')
) t(a);
SELECT * FROM bit_array_2d;

-- BYTEA (single dimension)
CREATE TABLE bytea_array_1d (a bytea[]);

INSERT INTO bytea_array_1d (a)
VALUES
    (ARRAY[decode('01020304', 'hex'), decode('aabbccdd', 'hex')]),
    (ARRAY[decode('11223344', 'hex'), decode('55667788', 'hex')]);
SELECT * FROM bytea_array_1d;

-- INTERVAL (two dimensions)
CREATE TABLE interval_array_2d(a INTERVAL[][]);
INSERT INTO interval_array_2d (a) VALUES (ARRAY[ARRAY['3 seconds', '5 minutes'], ARRAY['1 day', '2 hours']]::INTERVAL[][]);
SELECT * FROM interval_array_2d;

-- TIME (two dimensions)
CREATE TABLE time_array_2d(a TIME[][]);
INSERT INTO time_array_2d (a) VALUES (ARRAY[ARRAY['13:45:30', '08:15:00'], ARRAY['23:59:59', '00:00:00']]::TIME[][]);
SELECT * FROM time_array_2d;

-- TIMETZ (two dimensions)
CREATE TABLE timetz_array_2d(a TIMETZ[][]);
INSERT INTO timetz_array_2d (a) VALUES (
  ARRAY[
    ARRAY['13:45:30+01'::TIMETZ, '08:15:00+02'::TIMETZ],
    ARRAY['23:59:59-03'::TIMETZ, '00:00:00-04'::TIMETZ]
  ]::TIMETZ[][]
);
SELECT * FROM timetz_array_2d;

-- TIMESTAMP (two dimensions)
CREATE TABLE timestamp_array_2d(a TIMESTAMP[][]);
INSERT INTO timestamp_array_2d VALUES
    ('{{"2023-01-01 12:00:00","2023-01-02 13:00:00"},{"2023-01-03 14:00:00","2023-01-04 15:00:00"}}'),
    ('{{"2023-02-01 09:00:00","2023-02-02 10:00:00","2023-02-03 11:00:00"},{"2023-02-04 12:00:00","2023-02-05 13:00:00","2023-02-06 14:00:00"}}'),
    (NULL),
    ('{}'),
    ('{{"2023-03-01 08:00:00","2023-03-02 09:00:00"},{NULL,"2023-03-04 11:00:00"}}');
SELECT * FROM timestamp_array_2d;

-- FLOAT4 (two dimensions)
CREATE TABLE float4_array_2d(a FLOAT4[][]);
INSERT INTO float4_array_2d VALUES
    ('{{1.1,2.2},{3.3,4.4}}'),
    ('{{5.5,6.6,7.7},{8.8,9.9,10.1}}'),
    (NULL),
    ('{}'),
    ('{{11.1,12.2},{NULL,14.4}}');
SELECT * FROM float4_array_2d;

-- FLOAT8 (two dimensions)
CREATE TABLE float8_array_2d(a FLOAT8[][]);
INSERT INTO float8_array_2d VALUES
    ('{{1.11111,2.22222},{3.33333,4.44444}}'),
    ('{{5.55555,6.66666,7.77777},{8.88888,9.99999,10.10101}}'),
    (NULL),
    ('{}'),
    ('{{11.11111,12.22222},{NULL,14.44444}}');
SELECT * FROM float8_array_2d;

-- NUMERIC (two dimensions)
CREATE TABLE numeric_array_2d(a NUMERIC[][]);
INSERT INTO numeric_array_2d VALUES
    ('{{1.1,2.2},{3.3,4.4}}'),
    ('{{5.5,6.6,7.7},{8.8,9.9,10.1}}'),
    (NULL),
    ('{}'),
    ('{{11.1,12.2},{NULL,14.4}}');
SELECT * FROM numeric_array_2d;
SET duckdb.convert_unsupported_numeric_to_double = true;
SELECT * FROM numeric_array_2d;
RESET duckdb.convert_unsupported_numeric_to_double;

-- UUID (two dimensions)
CREATE TABLE uuid_array_2d(a UUID[][]);
INSERT INTO uuid_array_2d VALUES
    ('{{"a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11","a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a12"},{"a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a13","a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a14"}}'),
    ('{{"b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11","b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a12","b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a13"},{"b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a14","b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a15","b0eebc99-9c0b-4ef8-bb6d-6bb9bd380a16"}}'),
    (NULL),
    ('{}'),
    ('{{"c0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11","c0eebc99-9c0b-4ef8-bb6d-6bb9bd380a12"},{NULL,"c0eebc99-9c0b-4ef8-bb6d-6bb9bd380a14"}}');
SELECT * FROM uuid_array_2d;

-- REGCLASS (two dimensions)
CREATE TABLE regclass_array_2d(a REGCLASS[][]);
INSERT INTO regclass_array_2d VALUES
    ('{{"pg_class","pg_attribute"},{"pg_type","pg_index"}}'),
    ('{{"pg_proc","pg_operator","pg_aggregate"},{"pg_am","pg_amop","pg_amproc"}}'),
    (NULL),
    ('{}'),
    ('{{"pg_database","pg_tablespace"},{NULL,"pg_auth_members"}}');
SELECT * FROM regclass_array_2d;

-- Complex DuckDB array types testing

-- STRUCT arrays
SELECT * FROM duckdb.query($$
SELECT
    [{'name': 'Alice', 'age': 30}, {'name': 'Bob', 'age': 25}] as people_array,
    [{'name': 'Charlie', 'age': 35}] as single_person,
    CAST([] AS STRUCT(name VARCHAR, age INTEGER)[]) as empty_array
$$);

-- UNION arrays
SELECT * FROM duckdb.query($$
SELECT
    [union_value(str := 'hello'), union_value(str := 'world')] as string_union_array,
    [union_value(num := 42), union_value(num := 100)] as number_union_array,
    [CAST(union_value(num := 42) AS UNION(str VARCHAR, num INTEGER)), union_value(str := 'a100')] as mixed_union_array,
    CAST([] AS UNION(str VARCHAR, num INTEGER)[]) as empty_union_array
$$);

-- MAP arrays
SELECT * FROM duckdb.query($$
SELECT
    [map(['key1', 'key2'], [10, 20]), map(['a'], [100])] as map_array,
    [map(['x', 'y', 'z'], [1, 2, 3])] as single_map,
    CAST([] AS MAP(VARCHAR, INTEGER)[]) as empty_map_array
$$);

CREATE TABLE text_array_ctas AS SELECT * FROM duckdb.query($$
    SELECT ['box office', 'hollywood', '2025 Predictions', 'Movies', 'Culture', 'Best of 2025'] as tags
$$);

CREATE TABLE json_array_ctas AS SELECT * FROM duckdb.query($$
    SELECT [{'key': 'value'}, {'foo': 'bar'}]::JSON[] as data
$$);

SELECT * FROM text_array_ctas;
SELECT * FROM json_array_ctas;

-- Check column types
SELECT attname, atttypid::regtype
FROM pg_attribute
WHERE attrelid IN ('text_array_ctas'::regclass, 'json_array_ctas'::regclass)
  AND attnum > 0
ORDER BY attrelid::regclass::text, attname;

-- This was crashing before treating DuckDB varchar array as text array
ANALYZE text_array_ctas;
ANALYZE json_array_ctas;

-- Cleanup
DROP TABLE int_array_0d;
DROP TABLE int_array_1d;
DROP TABLE int_array_2d;
DROP TABLE bigint_array_1d;
DROP TABLE bool_array_1d;
DROP TABLE char_array_1d;
DROP TABLE smallint_array_1d;
DROP TABLE varchar_array_1d;
DROP TABLE varbit_array_1d;
DROP TABLE bit_array_1d;
DROP TABLE interval_array_1d;
DROP TABLE time_array_1d;
DROP TABLE timetz_array_1d;
DROP TABLE timestamp_array_1d;
DROP TABLE float4_array_1d;
DROP TABLE float8_array_1d;
DROP TABLE numeric_array_1d;
DROP TABLE uuid_array_1d;
DROP TABLE json_array_1d;
DROP TABLE jsonb_array_1d;
DROP TABLE text_array_ctas;
DROP TABLE json_array_ctas;
DROP TABLE regclass_array_1d;
DROP TABLE char_array_2d;
DROP TABLE smallint_array_2d;
DROP TABLE varchar_array_2d;
DROP TABLE varbit_array_2d;
DROP TABLE bit_array_2d;
DROP TABLE interval_array_2d;
DROP TABLE time_array_2d;
DROP TABLE timetz_array_2d;
DROP TABLE timestamp_array_2d;
DROP TABLE float4_array_2d;
DROP TABLE float8_array_2d;
DROP TABLE numeric_array_2d;
DROP TABLE uuid_array_2d;
DROP TABLE regclass_array_2d;
