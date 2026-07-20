--
-- ALTER_TYPE
--

-- Taken from Postgres test/regress/sql/yb.port.alter_table.sql

--
-- composite types
--

CREATE TYPE test_type AS (a int);

-- ALTER TYPE test_type ADD ATTRIBUTE b text;

-- ALTER TYPE test_type ALTER ATTRIBUTE b SET DATA TYPE varchar;

-- ALTER TYPE test_type ALTER ATTRIBUTE b SET DATA TYPE integer;
--- YB: since we can't ALTER TYPE ADD ATTRIBUTE yet, work around by recreating the type.
DROP TYPE test_type;
CREATE TYPE test_type AS (a int, b int);

ALTER TYPE test_type DROP ATTRIBUTE b;

ALTER TYPE test_type DROP ATTRIBUTE IF EXISTS c;

-- ALTER TYPE test_type DROP ATTRIBUTE a, ADD ATTRIBUTE d boolean;

-- ALTER TYPE test_type RENAME ATTRIBUTE a TO aa;
-- ALTER TYPE test_type RENAME ATTRIBUTE d TO dd;

CREATE TYPE test_type1 AS (a int, b text);
CREATE TABLE test_tbl1 (x int, y test_type1);

DROP TABLE test_tbl1;
CREATE TABLE test_tbl1 (x int, y text);
-- CREATE INDEX test_tbl1_idx ON test_tbl1((row(x,y)::test_type1));

CREATE TYPE test_type2 AS (a int, b text);
CREATE TABLE test_tbl2 OF test_type2;
CREATE TABLE test_tbl2_subclass () INHERITS (test_tbl2);

-- ALTER TYPE test_type2 ADD ATTRIBUTE c text CASCADE;

-- ALTER TYPE test_type2 ALTER ATTRIBUTE b TYPE varchar CASCADE;

-- ALTER TYPE test_type2 DROP ATTRIBUTE b CASCADE;

-- ALTER TYPE test_type2 RENAME ATTRIBUTE a TO aa CASCADE;

CREATE TYPE test_typex AS (a int, b text);
CREATE TABLE test_tblx (x int, y test_typex check ((y).a > 0));
ALTER TYPE test_typex DROP ATTRIBUTE a CASCADE;

-- This test isn't that interesting on its own, but the purpose is to leave
-- behind a table to test pg_upgrade with. The table has a composite type
-- column in it, and the composite type has a dropped attribute.
CREATE TYPE test_type3 AS (a int);
CREATE TABLE test_tbl3 (c) AS SELECT '(1)'::test_type3;
-- ALTER TYPE test_type3 DROP ATTRIBUTE a, ADD ATTRIBUTE b int;

CREATE TYPE test_type_empty AS ();
