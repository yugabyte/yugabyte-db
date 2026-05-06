-- CHAR
CREATE TABLE chr(a CHAR);
INSERT INTO chr SELECT CAST(a AS CHAR) from (VALUES (-128), (0), (127)) t(a);
SELECT * FROM chr;

-- SMALLINT
CREATE TABLE small(a SMALLINT);
INSERT INTO small SELECT CAST(a AS SMALLINT) from (VALUES (-32768), (0), (32767)) t(a);
SELECT * FROM small;

-- INTEGER
CREATE TABLE intgr(a INTEGER);
INSERT INTO intgr SELECT CAST(a AS INTEGER) from (VALUES (-2147483648), (0), (2147483647)) t(a);
SELECT * FROM intgr;

-- BIGINT
CREATE TABLE big(a BIGINT);
INSERT INTO big SELECT CAST(a AS BIGINT) from (VALUES (-9223372036854775808), (0), (9223372036854775807)) t(a);
SELECT * FROM big;

--- BOOL
CREATE TABLE bool_tbl(a BOOL);
INSERT INTO bool_tbl SELECT CAST(a AS BOOL) from (VALUES (False), (NULL), (True)) t(a);
SELECT * FROM bool_tbl;

--- VARCHAR
CREATE TABLE bpchar_tbl(a CHAR(25) NOT NULL);
INSERT INTO bpchar_tbl SELECT CAST(a AS VARCHAR) from (VALUES (''), ('test'), ('this is a long string')) t(a);
SELECT * FROM bpchar_tbl;
SELECT * FROM bpchar_tbl WHERE a = 'test';

--- VARCHAR
CREATE TABLE varchar_tbl(a VARCHAR);
INSERT INTO varchar_tbl SELECT CAST(a AS VARCHAR) from (VALUES (''), (NULL), ('test'), ('this is a long string')) t(a);
SELECT * FROM varchar_tbl;
SELECT * FROM varchar_tbl WHERE a = 'test';


--- TEXT
CREATE TABLE text_tbl(a TEXT);
INSERT INTO text_tbl SELECT CAST(a AS TEXT) from (VALUES (''), (NULL), ('test'), ('this is a long string')) t(a);
SELECT * FROM text_tbl;
SELECT * FROM text_tbl WHERE a = 'test';

-- DATE
CREATE TABLE date_tbl(a DATE);
INSERT INTO date_tbl SELECT CAST(a AS DATE) FROM (VALUES ('2022-04-29'::DATE), (NULL), ('2023-05-15'::DATE)) t(a);
SELECT * FROM date_tbl;

-- INTERVAL
CREATE TABLE interval_tbl(a INTERVAL);
INSERT INTO interval_tbl SELECT CAST(a AS INTERVAL) FROM (VALUES ('2 years 5 months 1 day 3 hours 30 minutes 5 seconds'::INTERVAL), ('5 day 5 hours'::INTERVAL), (NULL)) t(a);
SELECT * FROM interval_tbl;
SELECT * FROM interval_tbl WHERE a = '5 day 5 hours'::INTERVAL;

-- VARBIT
CREATE TABLE varbit_tbl(a VARBIT);
-- Insert a few kinds of bitstrings: (1) less than 8 bits; (2) equal to 8 bits; (3) larger than 8 bits.
INSERT INTO varbit_tbl SELECT CAST(a AS VARBIT) FROM (VALUES (B'1010'::VARBIT), (B'10100011'::VARBIT), (B'1010001011'::VARBIT), (NULL)) t(a);
SELECT * FROM varbit_tbl;

CREATE TABLE varbit20_tbl(a BIT VARYING(20));
-- Insert a few kinds of bitstrings: (1) less than 8 bits; (2) equal to 8 bits; (3) larger than 8 bits.
INSERT INTO varbit20_tbl SELECT CAST(a AS VARBIT) FROM (VALUES (B'1010'::VARBIT), (B'10100011'::VARBIT), (B'1010001011'::VARBIT), (NULL)) t(a);
SELECT * FROM varbit20_tbl;

-- BIT
CREATE TABLE bit_tbl(a BIT(4));
INSERT INTO bit_tbl VALUES (B'1010'), (B'0101'), (NULL);
SELECT * FROM bit_tbl;

CREATE TABLE bit14_tbl(a BIT(14));
INSERT INTO bit14_tbl VALUES (B'10101010101010'), (B'11111111111111'), (NULL);
SELECT * FROM bit14_tbl;

-- TIME
CREATE TABLE time_tbl(a TIME);
INSERT INTO time_tbl SELECT CAST(a AS TIME) FROM (VALUES ('13:45:30'::TIME), ('08:15:00'::TIME), (NULL)) t(a);
SELECT * FROM time_tbl;
SELECT * FROM time_tbl WHERE a = '08:15:00'::TIME;

-- TIMETZ
CREATE TABLE timetz_tbl(a TIMETZ);
INSERT INTO timetz_tbl SELECT CAST(a AS TIMETZ) FROM (VALUES ('13:45:30+01'::TIMETZ), ('08:15:00-05'::TIMETZ), (NULL)) t(a);
SELECT * FROM timetz_tbl;
SELECT * FROM timetz_tbl WHERE a = '08:15:00-05'::TIMETZ;

-- TIMESTAMP
CREATE TABLE timestamp_tbl(a TIMESTAMP);
INSERT INTO timestamp_tbl SELECT CAST(a AS TIMESTAMP) FROM (VALUES
    ('2022-04-29 10:15:30'::TIMESTAMP),
    (NULL),
    ('2023-05-15 12:30:45'::TIMESTAMP)
) t(a);
SELECT * FROM timestamp_tbl;

-- TIMESTAMP_TZ
CREATE TABLE timestamptz_tbl(a TIMESTAMP WITH TIME ZONE);
INSERT INTO timestamptz_tbl SELECT CAST(a AS TIMESTAMP WITH TIME ZONE) FROM (VALUES
    (NULL),
    ('2024-10-14 12:00:00'::TIMESTAMP WITH TIME ZONE),
    ('2024-10-14 12:00:00 Europe/London'::TIMESTAMP WITH TIME ZONE),
    ('2024-10-14 12:00:00 America/New_York'::TIMESTAMP WITH TIME ZONE),
    ('2024-10-14 12:00:00+06'::TIMESTAMP WITH TIME ZONE),
    ('2024-10-14 12:00:00-2'::TIMESTAMP WITH TIME ZONE),
    ('2024-10-14 12:00:00 CET'::TIMESTAMP WITH TIME ZONE)
) t(a);

SELECT * FROM timestamptz_tbl;
SELECT * FROM timestamptz_tbl WHERE a >= '2024-10-14 13:00:00+1';
SELECT * FROM timestamptz_tbl WHERE a >= '2024-10-14 13:00:00 Europe/London';

-- TIMESTAMP_NS Conversion from DuckDB to PostgreSQL
SELECT * FROM duckdb.query($$ SELECT '1992-12-12 12:12:12.123456789'::TIMESTAMP_NS as ts $$);

-- TIMESTAMP_MS Conversion from DuckDB to PostgreSQL
SELECT * FROM duckdb.query($$ SELECT '1992-12-12 12:12:12.123'::TIMESTAMP_MS as ts $$);

-- TIMESTAMP_S Conversion from DuckDB to PostgreSQL
SELECT * FROM duckdb.query($$ SELECT '1992-12-12 12:12:12'::TIMESTAMP_S as ts $$);

-- FLOAT4
CREATE TABLE float4_tbl(a FLOAT4);
INSERT INTO float4_tbl SELECT CAST(a AS FLOAT4) FROM (VALUES
    (0.234234234::FLOAT4),
    (NULL),
    (458234502034234234234.000012::FLOAT4)
) t(a);
SELECT * FROM float4_tbl;

-- FLOAT8
CREATE TABLE float8_tbl(a FLOAT8);
INSERT INTO float8_tbl SELECT CAST(a AS FLOAT8) FROM (VALUES
    (0.234234234::FLOAT8),
    (NULL),
    (458234502034234234234.000012::FLOAT8)
) t(a);
SELECT * FROM float8_tbl;

-- NUMERIC as DOUBLE
CREATE TABLE numeric_as_double(a NUMERIC);
INSERT INTO numeric_as_double SELECT a FROM (VALUES
    (0.234234234),
    (NULL),
    (458234502034234234234.000012)
) t(a);
-- Should fail
SELECT * FROM numeric_as_double;
-- Expressions involving such columns should also fail
SELECT 'yes' FROM numeric_as_double WHERE a > 10;
SET duckdb.convert_unsupported_numeric_to_double = true;
SELECT * FROM numeric_as_double;
RESET duckdb.convert_unsupported_numeric_to_double;

-- NUMERIC with a physical type of SMALLINT
CREATE TABLE smallint_numeric(a NUMERIC(4, 2));
INSERT INTO smallint_numeric SELECT a FROM (VALUES
    (0.23),
    (NULL),
    (45.12)
) t(a);
SELECT * FROM smallint_numeric;

-- NUMERIC with a physical type of INTEGER
CREATE TABLE integer_numeric(a NUMERIC(9, 6));
INSERT INTO integer_numeric SELECT a FROM (VALUES
    (243.345035::NUMERIC(9,6)),
    (NULL),
    (45.000012::NUMERIC(9,6))
) t(a);
SELECT * FROM integer_numeric;

-- NUMERIC with a physical type of BIGINT
CREATE TABLE bigint_numeric(a NUMERIC(18, 12));
INSERT INTO bigint_numeric SELECT a FROM (VALUES
    (856324.111122223333::NUMERIC(18,12)),
    (NULL),
    (12.000000000001::NUMERIC(18,12))
) t(a);
SELECT * FROM bigint_numeric;

-- NUMERIC with a physical type of HUGEINT
CREATE TABLE hugeint_numeric(a NUMERIC(38, 24));
INSERT INTO hugeint_numeric SELECT a FROM (VALUES
    (32942348563242.111222333444555666777888::NUMERIC(38,24)),
    (NULL),
    (123456789.000000000000000000000001::NUMERIC(38,24))
) t(a);
SELECT * FROM hugeint_numeric;

-- UUID
CREATE TABLE uuid_tbl(a UUID);
INSERT INTO uuid_tbl SELECT CAST(a as UUID) FROM (VALUES
    ('80bf0be9-89be-4ef8-bc58-fc7d691c5544'),
    (NULL),
    ('00000000-0000-0000-0000-000000000000')
) t(a);
SELECT * FROM uuid_tbl;

-- JSON
CREATE TABLE json_tbl(a JSON);
INSERT INTO json_tbl SELECT CAST(a as JSON) FROM (VALUES
    ('{"key1": "value1", "key2": "value2"}'),
    ('["item1", "item2", "item3"]'),
    (NULL),
    ('{}')
) t(a);
SELECT * FROM json_tbl;

-- JSONB
CREATE TABLE jsonb_tbl(a JSONB);
INSERT INTO jsonb_tbl (a) VALUES
('{"a": 1, "b": {"c": 2, "d": [3, 4]}, "e": "hello"}'),
('{"f": 10, "g": {"h": 20, "i": 30}, "j": [40, 50, 60]}'),
('{"k": true, "l": null, "m": {"n": "world", "o": [7, 8, 9]}}'),
('[1, 2, 3]'),
('["a", "b", "c"]'),
('[{"key": "value"}, {"key": "another"}]');
SELECT * FROM jsonb_tbl;

-- BLOB
CREATE TABLE blob_tbl(a bytea);
INSERT INTO blob_tbl SELECT CAST(a as bytea) FROM (VALUES
    ('\x'),
    ('\x110102030405060708090a0b0c0d0e0f'),
    (''),
    ('\x00'),
    ('\x07'),
    (NULL)
) t(a);
SELECT * from blob_tbl;
SELECT * from blob_tbl where a = '\x07';

-- REGCLASSOID
CREATE TABLE regclass_tbl (a REGCLASS);
INSERT INTO regclass_tbl VALUES (42), (3000000000);
SELECT * FROM regclass_tbl;

DROP TABLE chr;
DROP TABLE small;
DROP TABLE intgr;
DROP TABLE big;
DROP TABLE bpchar_tbl;
DROP TABLE varchar_tbl;
DROP TABLE text_tbl;
DROP TABLE date_tbl;
DROP TABLE interval_tbl;
DROP TABLE varbit_tbl;
DROP TABLE varbit20_tbl;
DROP TABLE bit_tbl;
DROP TABLE time_tbl;
DROP TABLE timetz_tbl;
DROP TABLE timestamp_tbl;
DROP TABLE timestamptz_tbl;
DROP TABLE float4_tbl;
DROP TABLE float8_tbl;
DROP TABLE numeric_as_double;
DROP TABLE smallint_numeric;
DROP TABLE integer_numeric;
DROP TABLE bigint_numeric;
DROP TABLE hugeint_numeric;
DROP TABLE uuid_tbl;
DROP TABLE json_tbl;
DROP TABLE jsonb_tbl;
DROP TABLE blob_tbl;
DROP TABLE regclass_tbl;
