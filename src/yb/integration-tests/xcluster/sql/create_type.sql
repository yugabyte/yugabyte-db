--
-- Creating types
--

-- Taken from postgres/src/test/regress/sql/yb.orig.create_type.sql
-- Taken from postgres/src/test/regress/sql/create_type.sql


--
-- Create shell type
--
CREATE TYPE shell;


--
-- Create composite type
--

CREATE TYPE composite_type AS (
  i INT,
  v VARCHAR
);

CREATE TYPE composite_type2;
CREATE TYPE composite_type2 AS (
  i INT,
  v VARCHAR
);


--
-- Create enum type
--
-- See separate create_enum.sql file
--

CREATE TYPE empty_enum;
CREATE TYPE empty_enum AS ENUM ();


--
-- Create range type
--
CREATE TYPE range_type AS RANGE (
    subtype = float
);

CREATE TYPE two_ints AS (a INT, b INT);
CREATE TYPE two_ints_range AS RANGE (SUBTYPE = two_ints);

CREATE TYPE textrange1 AS RANGE(SUBTYPE=TEXT, MULTIRANGE_TYPE_NAME=multirange_of_text, COLLATION="C");
-- should pass, because existing _textrange1 is automatically renamed
CREATE TYPE textrange2 AS RANGE(SUBTYPE=TEXT, MULTIRANGE_TYPE_NAME=_textrange1, COLLATION="C");

CREATE TYPE range_type_in_two_steps;
CREATE TYPE range_type_in_two_steps AS RANGE (
    subtype = float
);


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


-- Composite types with the same name but different schemas
CREATE SCHEMA schema1;
CREATE SCHEMA schema2;

CREATE TYPE schema1.composite_type_in_schema AS (
  i INT,
  v VARCHAR
);
CREATE TYPE schema2.composite_type_in_schema AS (
  i INT,
  v VARCHAR
);


-- Types with weird names
CREATE TYPE "funny type +" AS (i int);
CREATE TYPE "inside ""quotes""!" AS (i int);
CREATE TYPE "inside ""quotes"" 'here'!" AS (i int);
CREATE TYPE "CREATE" AS (i int);   -- use keyword
