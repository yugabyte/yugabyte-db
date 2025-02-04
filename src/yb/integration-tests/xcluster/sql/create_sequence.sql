--
-- Creating sequences
--

-- Taken from postgres/src/test/regress/sql/yb_pg_sequence.sql

--
-- CREATE SEQUENCE
--

-- sequence data types
CREATE SEQUENCE sequence_test5 AS integer;
CREATE SEQUENCE sequence_test6 AS smallint;
CREATE SEQUENCE sequence_test7 AS bigint;
CREATE SEQUENCE sequence_test8 AS integer MAXVALUE 100000;
CREATE SEQUENCE sequence_test9 AS integer INCREMENT BY -1;
CREATE SEQUENCE sequence_test10 AS integer MINVALUE -100000 START 1;
CREATE SEQUENCE sequence_test11 AS smallint;
CREATE SEQUENCE sequence_test12 AS smallint INCREMENT -1;
CREATE SEQUENCE sequence_test13 AS smallint MINVALUE -32768;
CREATE SEQUENCE sequence_test14 AS smallint MAXVALUE 32767 INCREMENT -1;

ALTER SEQUENCE sequence_test5 AS smallint;  -- success, max will be adjusted
ALTER SEQUENCE sequence_test8 AS smallint MAXVALUE 20000;  -- ok now
ALTER SEQUENCE sequence_test9 AS smallint;  -- success, min will be adjusted
ALTER SEQUENCE sequence_test10 AS smallint MINVALUE -20000;  -- ok now

ALTER SEQUENCE sequence_test11 AS int;  -- max will be adjusted
ALTER SEQUENCE sequence_test12 AS int;  -- min will be adjusted
ALTER SEQUENCE sequence_test13 AS int;  -- min and max will be adjusted
ALTER SEQUENCE sequence_test14 AS int;  -- min and max will be adjusted

---
--- test creation of SERIAL column
---

CREATE TABLE serialTest1 (f1 text, f2 serial);

INSERT INTO serialTest1 VALUES ('foo');
INSERT INTO serialTest1 VALUES ('bar');
INSERT INTO serialTest1 VALUES ('force', 100);

SELECT pg_get_serial_sequence('serialTest1', 'f2');

-- test smallserial / bigserial
CREATE TABLE serialTest2 (f1 text, f2 serial, f3 smallserial, f4 serial2,
  f5 bigserial, f6 serial8);

INSERT INTO serialTest2 (f1)
  VALUES ('test_defaults');

INSERT INTO serialTest2 (f1, f2, f3, f4, f5, f6)
  VALUES ('test_max_vals', 2147483647, 32767, 32767, 9223372036854775807,
          9223372036854775807),
         ('test_min_vals', -2147483648, -32768, -32768, -9223372036854775808,
          -9223372036854775808);

SELECT nextval('serialTest2_f2_seq');
SELECT nextval('serialTest2_f3_seq');
SELECT nextval('serialTest2_f4_seq');
SELECT nextval('serialTest2_f5_seq');
SELECT nextval('serialTest2_f6_seq');

-- basic sequence operations using both text and oid references
CREATE SEQUENCE sequence_test;
CREATE SEQUENCE IF NOT EXISTS sequence_test;

SELECT nextval('sequence_test'::text);
SELECT nextval('sequence_test'::regclass);
SELECT currval('sequence_test'::text);
SELECT currval('sequence_test'::regclass);
SELECT setval('sequence_test'::text, 32);
SELECT nextval('sequence_test'::regclass);
SELECT setval('sequence_test'::text, 99, false);
SELECT nextval('sequence_test'::regclass);
SELECT setval('sequence_test'::regclass, 32);
SELECT nextval('sequence_test'::text);
SELECT setval('sequence_test'::regclass, 99, false);
SELECT nextval('sequence_test'::text);
DISCARD SEQUENCES;

-- renaming sequences
CREATE SEQUENCE foo_seq;
ALTER TABLE foo_seq RENAME TO foo_seq_new;
SELECT nextval('foo_seq_new');
SELECT nextval('foo_seq_new');

-- renaming sequences
CREATE SEQUENCE foo_seq2;
ALTER SEQUENCE foo_seq2 RENAME TO foo_seq2_new;
SELECT nextval('foo_seq2_new');
SELECT nextval('foo_seq2_new');

-- renaming serial sequences
ALTER SEQUENCE serialtest1_f2_seq RENAME TO serialtest1_f2_foo;
INSERT INTO serialTest1 VALUES ('more');

--
-- Check dependencies of serial and ordinary sequences
--
CREATE TEMP SEQUENCE myseq2;
CREATE TEMP SEQUENCE myseq3;
CREATE TEMP TABLE t1 (
  f1 serial,
  f2 int DEFAULT nextval('myseq2'),
  f3 int DEFAULT nextval('myseq3'::text)
);

-- This however will work:
DROP SEQUENCE myseq3;
DROP TABLE t1;
-- Now OK:
DROP SEQUENCE myseq2;

--
-- Alter sequence
--
-- TODO(#24080): deal with these test cases appropriately once we
-- figure out which of these alters we are handling.
--

-- ALTER SEQUENCE IF EXISTS sequence_test2 RESTART WITH 24
--   INCREMENT BY 4 MAXVALUE 36 MINVALUE 5 CYCLE;

-- CREATE SEQUENCE sequence_test2 START WITH 32;

-- SELECT nextval('sequence_test2');

-- ALTER SEQUENCE sequence_test2 RESTART;
-- SELECT nextval('sequence_test2');

-- -- test CYCLE and NO CYCLE
-- ALTER SEQUENCE sequence_test2 RESTART WITH 24
--   INCREMENT BY 4 MAXVALUE 36 MINVALUE 5 CYCLE;
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');  -- cycled

-- ALTER SEQUENCE sequence_test2 RESTART WITH 24
--   NO CYCLE;
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');

-- ALTER SEQUENCE sequence_test2 RESTART WITH -24 START WITH -24
--   INCREMENT BY -4 MINVALUE -36 MAXVALUE -5 CYCLE;
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');  -- cycled

-- ALTER SEQUENCE sequence_test2 RESTART WITH -24
--   NO CYCLE;
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');
-- SELECT nextval('sequence_test2');

-- -- reset
-- ALTER SEQUENCE IF EXISTS sequence_test2 RESTART WITH 32 START WITH 32
--   INCREMENT BY 4 MAXVALUE 36 MINVALUE 5 CYCLE;

-- SELECT setval('sequence_test2', 5);


-- Test comments
COMMENT ON SEQUENCE sequence_test5 IS 'will work';
COMMENT ON SEQUENCE sequence_test5 IS NULL;

-- Test lastval()
CREATE SEQUENCE seq;
SELECT nextval('seq');
SELECT lastval();
SELECT setval('seq', 99);
SELECT lastval();
DISCARD SEQUENCES;

CREATE SEQUENCE seq2;
SELECT nextval('seq2');
SELECT lastval();

-- unlogged sequences
-- (more tests in src/test/recovery/)
CREATE UNLOGGED SEQUENCE sequence_test_unlogged;
ALTER SEQUENCE sequence_test_unlogged SET LOGGED;
ALTER SEQUENCE sequence_test_unlogged SET UNLOGGED;

-- Test sequences in read-only transactions
CREATE TEMPORARY SEQUENCE sequence_test_temp1;
START TRANSACTION READ ONLY;
SELECT nextval('sequence_test_temp1');  -- ok
ROLLBACK;
START TRANSACTION READ ONLY;
SELECT setval('sequence_test_temp1', 1);  -- ok
ROLLBACK;

-- cache tests
CREATE SEQUENCE test_seq1 CACHE 10;
SELECT nextval('test_seq1');
SELECT nextval('test_seq1');
SELECT nextval('test_seq1');


-- Sequences with the same name but different schemas
CREATE SCHEMA schema1;
CREATE SCHEMA schema2;

CREATE SEQUENCE schema1.my_sequence START WITH 1 INCREMENT BY 1;
CREATE SEQUENCE schema2.my_sequence START WITH 10 INCREMENT BY 11;


-- Changing sequence ownership
CREATE SEQUENCE owned_sequence START 1;
ALTER SEQUENCE owned_sequence OWNER TO postgres;
CREATE SEQUENCE owned_sequence2 START 1;
ALTER TABLE owned_sequence2 OWNER TO postgres;

CREATE SEQUENCE owned_sequence3 START 1;
CREATE TABLE owning_table (
    name TEXT NOT NULL
);
ALTER SEQUENCE owned_sequence3 OWNED BY owning_table.name;


-- Changing sequence schemas
CREATE SEQUENCE schema1.sequence_changing_schemas START WITH 1 INCREMENT BY 1;
ALTER SEQUENCE schema1.sequence_changing_schemas SET SCHEMA schema2;
CREATE SEQUENCE schema1.sequence_changing_schemas2 START WITH 1 INCREMENT BY 1;
ALTER TABLE schema1.sequence_changing_schemas2 SET SCHEMA schema2;


-- Sequences created using a non-standard default schema
CREATE SCHEMA IF NOT EXISTS my_schema;
SET search_path TO my_schema;
CREATE SEQUENCE my_sequence;  -- actually my_schema.my_sequence
CREATE TABLE my_table (       -- actually my_schema.my_table
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL
);
