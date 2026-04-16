--
-- Creating enums
--

-- Taken from postgres/src/test/regress/sql/enum.sql

CREATE TYPE rainbow AS ENUM ('red', 'orange', 'yellow', 'green', 'blue', 'purple');


--
-- adding new values
--

CREATE TYPE planets AS ENUM ( 'venus', 'earth', 'mars' );

ALTER TYPE planets ADD VALUE 'uranus';

ALTER TYPE planets ADD VALUE 'mercury' BEFORE 'venus';
ALTER TYPE planets ADD VALUE 'saturn' BEFORE 'uranus';
ALTER TYPE planets ADD VALUE 'jupiter' AFTER 'mars';
ALTER TYPE planets ADD VALUE 'neptune' AFTER 'uranus';

ALTER TYPE planets ADD VALUE IF NOT EXISTS 'pluto';


--
-- Test inserting so many values that we have to renumber
-- Renumbering is not supported by YugabyteDB so skipping this test.
--
-- create type insenum as enum ('L1', 'L2');
-- 
-- alter type insenum add value 'i1' before 'L2';
-- alter type insenum add value 'i2' before 'L2';
-- alter type insenum add value 'i3' before 'L2';
-- alter type insenum add value 'i4' before 'L2';
-- alter type insenum add value 'i5' before 'L2';
-- alter type insenum add value 'i6' before 'L2';
-- alter type insenum add value 'i7' before 'L2';
-- alter type insenum add value 'i8' before 'L2';
-- alter type insenum add value 'i9' before 'L2';
-- alter type insenum add value 'i10' before 'L2';
-- alter type insenum add value 'i11' before 'L2';
-- alter type insenum add value 'i12' before 'L2';
-- alter type insenum add value 'i13' before 'L2';
-- alter type insenum add value 'i14' before 'L2';
-- alter type insenum add value 'i15' before 'L2';
-- alter type insenum add value 'i16' before 'L2';
-- alter type insenum add value 'i17' before 'L2';
-- alter type insenum add value 'i18' before 'L2';
-- alter type insenum add value 'i19' before 'L2';
-- alter type insenum add value 'i20' before 'L2';
-- alter type insenum add value 'i21' before 'L2';
-- alter type insenum add value 'i22' before 'L2';
-- alter type insenum add value 'i23' before 'L2';
-- alter type insenum add value 'i24' before 'L2';
-- alter type insenum add value 'i25' before 'L2';
-- alter type insenum add value 'i26' before 'L2';
-- alter type insenum add value 'i27' before 'L2';
-- alter type insenum add value 'i28' before 'L2';
-- alter type insenum add value 'i29' before 'L2';
-- alter type insenum add value 'i30' before 'L2';

--
-- Basic table creation
--
CREATE TABLE enumtest (col rainbow);
INSERT INTO enumtest values ('red'), ('orange'), ('yellow'), ('green');
COPY enumtest FROM stdin;
blue
purple
\.

--
-- Index tests, force use of index
--
SET enable_seqscan = off;
SET enable_bitmapscan = off;

--
-- Btree index / opclass with the various operators
--
CREATE UNIQUE INDEX enumtest_btree ON enumtest USING btree (col);

--
-- Hash index / opclass with the = operator
--
CREATE INDEX enumtest_hash ON enumtest USING hash (col);

--
-- End index tests
--
RESET enable_seqscan;
RESET enable_bitmapscan;

--
-- Domains over enums
--
CREATE DOMAIN rgb AS rainbow CHECK (VALUE IN ('red', 'green', 'blue'));

--
-- User functions, can't test perl/python etc here since may not be compiled.
--
CREATE FUNCTION echo_me(anyenum) RETURNS text AS $$
BEGIN
RETURN $1::text || 'omg';
END
$$ LANGUAGE plpgsql;
--
-- Concrete function should override generic one
--
CREATE FUNCTION echo_me(rainbow) RETURNS text AS $$
BEGIN
RETURN $1::text || 'wtf';
END
$$ LANGUAGE plpgsql;

--
-- RI triggers on enum types
--
CREATE TABLE enumtest_parent (id rainbow PRIMARY KEY);
CREATE TABLE enumtest_child (parent rainbow REFERENCES enumtest_parent);
INSERT INTO enumtest_parent VALUES ('red');
INSERT INTO enumtest_child VALUES ('red');

-- check renaming a value
ALTER TYPE rainbow RENAME VALUE 'red' TO 'crimson';

--
-- check transactional behaviour of ALTER TYPE ... ADD VALUE
--
-- NOTE: at least as of 1/2025, in YugabyteDB, ROLLBACK only rolls
-- back DMLs; DDLs are not rolled back.
--
-- TODO(Julien): once #23957 is done, uncomment the BEGIN & ROLLBACK statements below.
CREATE TYPE bogus AS ENUM('good');

BEGIN;
ALTER TYPE bogus ADD VALUE 'new';
SAVEPOINT x;
SELECT enum_first(null::bogus);  -- safe
ROLLBACK TO x;
COMMIT;

-- BEGIN;
ALTER TYPE bogus RENAME TO bogon;
ALTER TYPE bogon ADD VALUE 'bad';
-- ROLLBACK;

-- BEGIN;
CREATE TYPE bogus2 AS ENUM('good','bad','ugly');
ALTER TYPE bogus2 RENAME TO bogon2;
-- ROLLBACK;

-- BEGIN;
CREATE TYPE bogus3 AS ENUM('good');
ALTER TYPE bogus3 RENAME TO bogon3;
ALTER TYPE bogon3 ADD VALUE 'bad';
ALTER TYPE bogon3 ADD VALUE 'ugly';
-- ROLLBACK;


--
-- Test for handling to enums with overlapping labels
--

-- BEGIN;
CREATE TYPE colors AS ENUM ('red', 'orange', 'yellow', 'green', 'blue', 'purple');
CREATE TYPE paint_color AS ENUM ('red', 'orange', 'white');
-- COMMIT;


-- Test empty enum

CREATE TYPE empty_enum AS ENUM ();


CREATE TYPE huge_label AS ENUM ('exactly_63_character_identifier_1234567890abcdefghijklmnopqrstu');


-- Enums with the same name but different schemas
CREATE SCHEMA schema1;
CREATE SCHEMA schema2;

CREATE TYPE schema1.enum_in_schema AS ();
CREATE TYPE schema2.enum_in_schema AS ();
