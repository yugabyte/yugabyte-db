--
-- Create composite type
--
CREATE TYPE composite_type AS (
  i INT,
  v VARCHAR
);
\dT+ composite_type;

SELECT typname FROM pg_type WHERE typname LIKE '%composite_type';

SELECT (1, 'a')::composite_type;

-- Following should error
CREATE TABLE composite_table_indexed (a composite_type PRIMARY KEY, b BOOLEAN);
CREATE TABLE composite_table (a composite_type, b BOOLEAN);
INSERT INTO composite_table (a, b) VALUES ((2, 'bb')::composite_type, TRUE);
INSERT INTO composite_table (a) VALUES ((3, 'ccc')::composite_type);
INSERT INTO composite_table (b) VALUES (FALSE);
SELECT * FROM composite_table ORDER BY a;

--
-- Create enum type
--
CREATE TYPE enum_type AS ENUM ('baa', 'caw', 'moo');
\dT+ enum_type;

SELECT typname FROM pg_type WHERE typname LIKE '%enum_type';

SELECT 'moo'::enum_type;

CREATE TABLE enum_table (a enum_type, b BOOLEAN);
INSERT INTO enum_table (a) VALUES ('baa');
INSERT INTO enum_table (a, b) VALUES ('caw', FALSE);
INSERT INTO enum_table (b) VALUES (TRUE);
SELECT * FROM enum_table ORDER BY a;

--
-- Create range type
--
CREATE TYPE range_type AS RANGE (
    subtype = float
);
\dT+ range_type;

SELECT typname FROM pg_type WHERE typname LIKE '%range_type';

SELECT '[1.111, 2.222]'::range_type;

-- Following should error
CREATE TABLE range_table_indexed (a range_type PRIMARY KEY, b BOOLEAN);
CREATE TABLE range_table (a range_type, b BOOLEAN);
INSERT INTO range_table (a) VALUES ('[3.0, 4.8]');
-- Following should error
INSERT INTO range_table (a) VALUES ('[6.0, 3.8]');
INSERT INTO range_table (b) VALUES (TRUE);
INSERT INTO range_table (a, b) VALUES ('[1.4, 3.3]', FALSE);
SELECT * FROM range_table ORDER BY a;

-- Borrowed from https://www.postgresql.org/docs/11/rangetypes.html#RANGETYPES-BUILTIN
-- Containment
SELECT int4range(10, 20) @> 3;

-- Overlaps
SELECT numrange(11.1, 22.2) && numrange(20.0, 30.0);

-- Extract the upper bound
SELECT upper(int8range(15, 25));

-- Compute the intersection
SELECT int4range(10, 20) * int4range(15, 25);

-- Is the range empty?
SELECT isempty(numrange(1, 5));

--
-- Create base type
--
-- Taken from https://stackoverflow.com/questions/45188301/
CREATE TYPE base_type;
CREATE FUNCTION base_type_in(cstring) RETURNS base_type
   LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2in';
CREATE FUNCTION base_type_out(base_type) RETURNS cstring
   LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2out';
CREATE FUNCTION base_type_recv(internal) RETURNS base_type
   LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2recv';
CREATE FUNCTION base_type_send(base_type) RETURNS bytea
   LANGUAGE internal IMMUTABLE STRICT PARALLEL SAFE AS 'int2send';
CREATE TYPE base_type (
   INPUT = base_type_in,
   OUTPUT = base_type_out,
   RECEIVE = base_type_recv,
   SEND = base_type_send,
   LIKE = smallint,
   CATEGORY = 'N',
   PREFERRED = FALSE,
   DELIMITER = ',',
   COLLATABLE = FALSE
);
\dT+ base_type;

SELECT typname FROM pg_type WHERE typname LIKE '%base_type';

SELECT '32767'::base_type;

-- Following should error
SELECT '32768'::base_type;
-- Following should error
CREATE TABLE ints_indexed (a base_type PRIMARY KEY, b BOOLEAN);
CREATE TABLE base_table (a base_type, b BOOLEAN);
INSERT INTO base_table (b) VALUES (FALSE);
INSERT INTO base_table (a, b) VALUES ('1'::base_type, TRUE);
INSERT INTO base_table (a) VALUES ('22'::base_type);
SELECT * FROM base_table ORDER BY a;
SELECT * FROM base_table ORDER BY CAST(a AS text);

--
-- Summary
--
SELECT typname FROM pg_type WHERE oid > 16000 ORDER BY oid DESC LIMIT 18;

--
-- Drop composite type
--
-- Following should error
DROP TYPE composite_type;
DROP TYPE composite_type CASCADE;
\dT+ composite_type;

SELECT oid, typname FROM pg_type WHERE typname LIKE '%composite_type';

-- Following should error
SELECT (4, 'dddd')::composite_type;
-- Following should error
SELECT * FROM composite_table ORDER BY a;
SELECT * FROM composite_table ORDER BY b;

--
-- Drop enum type
--
DROP TYPE enum_type;
DROP TYPE enum_type CASCADE;
\dT+ enum_type;

SELECT oid, typname FROM pg_type WHERE typname LIKE '%enum_type';

-- Following should error
SELECT 'caw'::enum_type;
-- Following should error
SELECT * FROM enum_table ORDER BY a;
SELECT * FROM enum_table ORDER BY b;

--
-- Drop range type
--
DROP TYPE range_type;
DROP TYPE range_type CASCADE;
\dT+ range_type;

SELECT oid, typname FROM pg_type WHERE typname LIKE '%range_type';

-- Following should error
SELECT '[1.1, 2.2]'::range_type;
-- Following should error
SELECT * FROM range_table ORDER BY a;
SELECT * FROM range_table ORDER BY b;

--
-- Drop base type
--
\set VERBOSITY terse
DROP TYPE base_type;
DROP TYPE base_type CASCADE;
\set VERBOSITY default
\dT+ base_type;

SELECT oid, typname FROM pg_type WHERE typname LIKE '%base_type';

-- Following should error
SELECT '333'::base_type;
-- Following should error
SELECT * FROM base_table ORDER BY CAST(a AS text);
SELECT * FROM base_table ORDER BY b;

--
-- Summary
--
SELECT typname FROM pg_type WHERE oid > 16000 ORDER BY oid DESC LIMIT 8;

--
-- Cleanup
--
DROP TABLE composite_table, enum_table, range_table, base_table;
