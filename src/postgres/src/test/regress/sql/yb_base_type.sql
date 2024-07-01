--
-- Create base types exercising various parameters.
--

\set VERBOSITY terse

-- typlen:128, typbyval:f, typalign:c
CREATE TYPE bigname (
    INPUT = bigname_in,
    OUTPUT = bigname_out,
    INTERNALLENGTH = 128,
    ALIGNMENT = char,
    STORAGE = plain
);
CREATE TABLE bigname_table (t bigname);
INSERT INTO bigname_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaABCcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccC');
INSERT INTO bigname_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaABCcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccCB');
INSERT INTO bigname_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaABCcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccCBD');
SELECT * FROM bigname_table;
DROP TABLE bigname_table;
DROP TYPE bigname CASCADE;

-- typlen:64, typbyval:f, typalign:c
CREATE TYPE name_type;
CREATE FUNCTION name_type_in(cstring) RETURNS name_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'namein';
CREATE FUNCTION name_type_out(name_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'nameout';
CREATE TYPE name_type (
    INPUT = name_type_in,
    OUTPUT = name_type_out,
    LIKE = name
);
CREATE TABLE name_table (t name_type);
INSERT INTO name_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaA');
INSERT INTO name_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaAB');
INSERT INTO name_table (t)
    VALUES ('AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaABC');
SELECT * FROM name_table;
DROP TABLE name_table;
DROP TYPE name_type CASCADE;

-- typlen:32, typbyval:f, typalign:d
CREATE TYPE lseg_type;
CREATE FUNCTION lseg_type_in(cstring) RETURNS lseg_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'lseg_in';
CREATE FUNCTION lseg_type_out(lseg_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'lseg_out';
CREATE TYPE lseg_type (
    INPUT = lseg_type_in,
    OUTPUT = lseg_type_out,
    LIKE = lseg
);
CREATE TABLE lseg_table (t lseg_type);
INSERT INTO lseg_table (t)
    VALUES ('[(1, 2), (3, 4)]');
SELECT * FROM lseg_table;
DROP TABLE lseg_table;
DROP TYPE lseg_type CASCADE;

-- typlen:24, typbyval:f, typalign:d
CREATE TYPE line_type;
CREATE FUNCTION line_type_in(cstring) RETURNS line_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'line_in';
CREATE FUNCTION line_type_out(line_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'line_out';
CREATE TYPE line_type (
    INPUT = line_type_in,
    OUTPUT = line_type_out,
    LIKE = line
);
CREATE TABLE line_table (t line_type);
INSERT INTO line_table (t)
    VALUES ('{1, 2, 3}');
SELECT * FROM line_table;
DROP TABLE line_table;
DROP TYPE line_type CASCADE;

-- typlen:16, typbyval:f, typalign:d
CREATE TYPE point_type;
CREATE FUNCTION point_type_in(cstring) RETURNS point_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'point_in';
CREATE FUNCTION point_type_out(point_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'point_out';
CREATE TYPE point_type (
    INPUT = point_type_in,
    OUTPUT = point_type_out,
    LIKE = point
);
CREATE TABLE point_table (t point_type);
INSERT INTO point_table (t)
    VALUES ('(1, 2)');
SELECT * FROM point_table;
DROP TABLE point_table;
DROP TYPE point_type CASCADE;

-- typlen:16, typbyval:f, typalign:c
CREATE TYPE uuid_type;
CREATE FUNCTION uuid_type_in(cstring) RETURNS uuid_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'uuid_in';
CREATE FUNCTION uuid_type_out(uuid_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'uuid_out';
CREATE TYPE uuid_type (
    INPUT = uuid_type_in,
    OUTPUT = uuid_type_out,
    LIKE = uuid
);
CREATE TABLE uuid_table (t uuid_type);
INSERT INTO uuid_table (t)
    VALUES ('a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11');
SELECT * FROM uuid_table;
DROP TABLE uuid_table;
DROP TYPE uuid_type CASCADE;

-- typlen:12, typbyval:f, typalign:d
CREATE TYPE timetz_type;
CREATE FUNCTION timetz_type_in(cstring) RETURNS timetz_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'timetz_in';
CREATE FUNCTION timetz_type_out(timetz_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'timetz_out';
CREATE TYPE timetz_type (
    INPUT = timetz_type_in,
    OUTPUT = timetz_type_out,
    LIKE = timetz
);
CREATE TABLE timetz_table (t timetz_type);
-- Inspired by https://stackoverflow.com/questions/29993956/
INSERT INTO timetz_table (t)
    VALUES ('2015-05-01 11:25:00 America/Caracas');
SELECT * FROM timetz_table;
DROP TABLE timetz_table;
DROP TYPE timetz_type CASCADE;

-- typlen:8, typbyval:f, typalign:i
CREATE TYPE macaddr8_type;
CREATE FUNCTION macaddr8_type_in(cstring) RETURNS macaddr8_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'macaddr8_in';
CREATE FUNCTION macaddr8_type_out(macaddr8_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'macaddr8_out';
CREATE TYPE macaddr8_type (
    INPUT = macaddr8_type_in,
    OUTPUT = macaddr8_type_out,
    LIKE = macaddr8
);
CREATE TABLE macaddr8_table (t macaddr8_type);
INSERT INTO macaddr8_table (t)
    VALUES ('08:00:2b:01:02:03:04:05');
SELECT * FROM macaddr8_table;
DROP TABLE macaddr8_table;
DROP TYPE macaddr8_type CASCADE;

-- typlen:8, typbyval:t, typalign:d
CREATE TYPE int8_type;
CREATE FUNCTION int8_type_in(cstring) RETURNS int8_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int8in';
CREATE FUNCTION int8_type_out(int8_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int8out';
CREATE TYPE int8_type (
    INPUT = int8_type_in,
    OUTPUT = int8_type_out,
    LIKE = int8
);
CREATE TABLE int8_table (t int8_type);
INSERT INTO int8_table (t)
    VALUES ('9223372036854775807');
INSERT INTO int8_table (t)
    VALUES ('9223372036854775808');
SELECT * FROM int8_table;
DROP TABLE int8_table;
DROP TYPE int8_type CASCADE;

-- typlen:8, typbyval:t, typalign:d
CREATE TYPE float8_type;
CREATE FUNCTION float8_type_in(cstring) RETURNS float8_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'float8in';
CREATE FUNCTION float8_type_out(float8_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'float8out';
CREATE TYPE float8_type (
    INPUT = float8_type_in,
    OUTPUT = float8_type_out,
    LIKE = float8
);
CREATE TABLE float8_table (t float8_type);
INSERT INTO float8_table (t)
    VALUES ('1.23456789');
SELECT * FROM float8_table;
DROP TABLE float8_table;
DROP TYPE float8_type CASCADE;

-- typlen:6, typbyval:f, typalign:i
CREATE TYPE macaddr_type;
CREATE FUNCTION macaddr_type_in(cstring) RETURNS macaddr_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'macaddr_in';
CREATE FUNCTION macaddr_type_out(macaddr_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'macaddr_out';
CREATE TYPE macaddr_type (
    INPUT = macaddr_type_in,
    OUTPUT = macaddr_type_out,
    LIKE = macaddr
);
CREATE TABLE macaddr_table (t macaddr_type);
INSERT INTO macaddr_table (t)
    VALUES ('08:00:2b:01:02:03');
SELECT * FROM macaddr_table;
DROP TABLE macaddr_table;
DROP TYPE macaddr_type CASCADE;

-- typlen:6, typbyval:f, typalign:s
CREATE TYPE tid_type;
CREATE FUNCTION tid_type_in(cstring) RETURNS tid_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'tidin';
CREATE FUNCTION tid_type_out(tid_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'tidout';
CREATE TYPE tid_type (
    INPUT = tid_type_in,
    OUTPUT = tid_type_out,
    LIKE = tid
);
CREATE TABLE tid_table (t tid_type);
INSERT INTO tid_table (t)
    VALUES ('(0, 1)');
SELECT * FROM tid_table;
DROP TABLE tid_table;
DROP TYPE tid_type CASCADE;

-- typlen:4, typbyval:t, typalign:d
CREATE TYPE int4_type;
CREATE FUNCTION int4_type_in(cstring) RETURNS int4_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int4in';
CREATE FUNCTION int4_type_out(int4_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int4out';
CREATE TYPE int4_type (
    INPUT = int4_type_in,
    OUTPUT = int4_type_out,
    LIKE = int4
);
CREATE TABLE int4_table (t int4_type);
INSERT INTO int4_table (t)
    VALUES ('2147483647');
INSERT INTO int4_table (t)
    VALUES ('2147483648');
SELECT * FROM int4_table;
DROP TABLE int4_table;
DROP TYPE int4_type CASCADE;

-- typlen:2, typbyval:t, typalign:d
CREATE TYPE int2_type;
CREATE FUNCTION int2_type_in(cstring) RETURNS int2_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2in';
CREATE FUNCTION int2_type_out(int2_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2out';
CREATE TYPE int2_type (
    INPUT = int2_type_in,
    OUTPUT = int2_type_out,
    LIKE = int2
);
CREATE TABLE int2_table (t int2_type);
INSERT INTO int2_table (t)
    VALUES ('32767');
INSERT INTO int2_table (t)
    VALUES ('32768');
SELECT * FROM int2_table;
DROP TABLE int2_table;
DROP TYPE int2_type CASCADE;

-- typlen:1, typbyval:t, typalign:c
CREATE TYPE bool_type;
CREATE FUNCTION bool_type_in(cstring) RETURNS bool_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'boolin';
CREATE FUNCTION bool_type_out(bool_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'boolout';
CREATE TYPE bool_type (
    INPUT = bool_type_in,
    OUTPUT = bool_type_out,
    LIKE = bool
);
CREATE TABLE bool_table (t bool_type);
INSERT INTO bool_table (t)
    VALUES ('true');
SELECT * FROM bool_table;
DROP TABLE bool_table;
DROP TYPE bool_type CASCADE;

-- typlen:1, typbyval:t, typalign:c
CREATE TYPE char_type;
CREATE FUNCTION char_type_in(cstring) RETURNS char_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'charin';
CREATE FUNCTION char_type_out(char_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'charout';
CREATE TYPE char_type (
    INPUT = char_type_in,
    OUTPUT = char_type_out,
    INTERNALLENGTH = 1,
    PASSEDBYVALUE,
    ALIGNMENT = char,
    STORAGE = plain
);
CREATE TABLE char_table (t char_type);
INSERT INTO char_table (t)
    VALUES ('t');
SELECT * FROM char_table;
DROP TABLE char_table;
DROP TYPE char_type CASCADE;

-- typlen:-1, typbyval:f, typalign:i
CREATE TYPE bytea_type;
CREATE FUNCTION bytea_type_in(cstring) RETURNS bytea_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'byteain';
CREATE FUNCTION bytea_type_out(bytea_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'byteaout';
CREATE TYPE bytea_type (
    INPUT = bytea_type_in,
    OUTPUT = bytea_type_out,
    LIKE = bytea
);
CREATE TABLE bytea_table (t bytea_type);
INSERT INTO bytea_table (t)
    VALUES (E'\\xDEADBEEF');
SELECT * FROM bytea_table;
DROP TABLE bytea_table;
DROP TYPE bytea_type CASCADE;

-- typlen:-1, typbyval:f, typalign:i
CREATE TYPE cidr_type;
CREATE FUNCTION cidr_type_in(cstring) RETURNS cidr_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'cidr_in';
CREATE FUNCTION cidr_type_out(cidr_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'cidr_out';
CREATE TYPE cidr_type (
    INPUT = cidr_type_in,
    OUTPUT = cidr_type_out,
    LIKE = cidr
);
CREATE TABLE cidr_table (t cidr_type);
INSERT INTO cidr_table (t)
    VALUES ('192.168.100.128/25');
SELECT * FROM cidr_table;
DROP TABLE cidr_table;
DROP TYPE cidr_type CASCADE;

-- typlen:-1, typbyval:f, typalign:i
CREATE TYPE int2vector_type;
CREATE FUNCTION int2vector_type_in(cstring) RETURNS int2vector_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2vectorin';
CREATE FUNCTION int2vector_type_out(int2vector_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2vectorout';
CREATE TYPE int2vector_type (
    INPUT = int2vector_type_in,
    OUTPUT = int2vector_type_out,
    LIKE = int2vector
);
CREATE TABLE int2vector_table (t int2vector_type);
INSERT INTO int2vector_table (t)
    VALUES ('1 2 3');
SELECT * FROM int2vector_table;
DROP TABLE int2vector_table;
DROP TYPE int2vector_type CASCADE;

-- typlen:-1, typbyval:f, typalign:i
CREATE TYPE json_type;
CREATE FUNCTION json_type_in(cstring) RETURNS json_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'json_in';
CREATE FUNCTION json_type_out(json_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'json_out';
CREATE TYPE json_type (
    INPUT = json_type_in,
    OUTPUT = json_type_out,
    LIKE = json
);
CREATE TABLE json_table (t json_type);
INSERT INTO json_table (t)
    VALUES ('{"a": [1, 2, 3]}');
SELECT * FROM json_table;
DROP TABLE json_table;
DROP TYPE json_type CASCADE;

-- typlen:-1, typbyval:f, typalign:i
CREATE TYPE text_type;
CREATE FUNCTION text_type_in(cstring) RETURNS text_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'textin';
CREATE FUNCTION text_type_out(text_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'textout';
CREATE TYPE text_type (
    INPUT = text_type_in,
    OUTPUT = text_type_out,
    LIKE = text
);
CREATE TABLE text_table (t text_type);
INSERT INTO text_table (t)
    VALUES ('thisistext');
SELECT * FROM text_table;
DROP TABLE text_table;
DROP TYPE text_type CASCADE;

-- typlen:-2, typbyval:f, typalign:i
CREATE TYPE cstring_type;
CREATE FUNCTION cstring_type_in(cstring) RETURNS cstring_type
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'cstring_in';
CREATE FUNCTION cstring_type_out(cstring_type) RETURNS cstring
    LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'cstring_out';
CREATE TYPE cstring_type (
    INPUT = cstring_type_in,
    OUTPUT = cstring_type_out,
    LIKE = cstring
);
CREATE TABLE cstring_table (t cstring_type);
INSERT INTO cstring_table (t)
    VALUES ('thisiscstring');
SELECT * FROM cstring_table;
DROP TABLE cstring_table;
DROP TYPE cstring_type CASCADE;

\set VERBOSITY default
