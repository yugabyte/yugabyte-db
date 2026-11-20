-- this test must be run on a database named 'contrib_regression'
-- YB: the database doesn't need to be 'contrib_regression'
CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- INIT

BEGIN; -- YB: Workaround for read time error, check #25665
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665

SECURITY LABEL FOR anon ON SCHEMA pg_catalog IS 'TRUSTED';

CREATE ROLE oscar_the_owner LOGIN PASSWORD 'xlfneifzmqdef';
ALTER DATABASE :DBNAME OWNER TO oscar_the_owner;

SELECT anon.set_shift(2093049226);

SET ROLE oscar_the_owner;

CREATE SCHEMA test;

--
-- C. Unmasked Data
--
CREATE TABLE test.no_masks AS SELECT 1 AS i;

CREATE TABLE test.cards (
  id integer NOT NULL,
  board_id integer NOT NULL,
  data TEXT
);

INSERT INTO test.cards VALUES
(1, 1, 'Paint house'),
(2, 1, 'Clean'),
(3, 1, 'Cook'),
(4, 1, 'Vacuum'),
(999999,0, E'(,Very"Weird\'\'value\t trying\n to\,break '' CSV\)export)');

CREATE TABLE test.customer (
  id INT PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  name TEXT,
  "CreditCard" TEXT,
  height_cm NUMERIC,
  height_in NUMERIC GENERATED ALWAYS AS (height_cm / 2.54) STORED
);

--
-- D. Masked Data
--
INSERT INTO test.customer(name,"CreditCard",height_cm)
VALUES
('Schwarzenegger','1234567812345678',188),
('Stalone'       ,'2683464645336781',177),
('Lundgren'      ,'6877322588932345',192);

SECURITY LABEL FOR anon ON COLUMN test.customer.name
IS E'MASKED WITH FUNCTION pg_catalog.md5(''0'') ';

SECURITY LABEL FOR anon ON COLUMN test.customer."CreditCard"
IS E'MASKED WITH FUNCTION pg_catalog.md5(''0'') ';

CREATE TABLE test."COMPANY" (
  rn SERIAL,
  "IBAN" TEXT,
  BRAND TEXT
);

INSERT INTO test."COMPANY"
VALUES (1991,'12345677890','Cyberdyne Systems');

SECURITY LABEL FOR anon ON COLUMN test."COMPANY".rn
IS 'MASKED WITH FUNCTION anon.pseudo_xor(rn)';

SECURITY LABEL FOR anon ON COLUMN test."COMPANY"."IBAN"
IS E'MASKED WITH FUNCTION pg_catalog.md5(''0'') ';

SECURITY LABEL FOR anon ON COLUMN test."COMPANY".brand
IS E'MASKED WITH VALUE $$CONFIDENTIAL$$ ';

--
-- E. Sequences
--
CREATE SEQUENCE public.seq42;
ALTER SEQUENCE public.seq42 RESTART WITH 42;

--
-- F. Sampling
--
/* YB(#4288): TABLESAMPLE is not supported; ysql_dump of a sampled table errors
CREATE TABLE test.hundred AS
SELECT generate_series(1,100) AS h;

SECURITY LABEL FOR anon ON TABLE test.hundred
IS 'TABLESAMPLE BERNOULLI(33)';
*/ -- YB

RESET ROLE;

--
-- Prepare the dump_anon role
--
CREATE ROLE dump_anon LOGIN PASSWORD 'x';
ALTER ROLE dump_anon SET anon.transparent_dynamic_masking = True;
SECURITY LABEL FOR anon ON ROLE dump_anon IS 'MASKED';

GRANT USAGE ON SCHEMA public TO dump_anon;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO dump_anon;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA public TO dump_anon;

GRANT USAGE ON SCHEMA test TO dump_anon;
GRANT SELECT ON ALL TABLES IN SCHEMA test TO dump_anon;
GRANT SELECT ON ALL SEQUENCES IN SCHEMA test TO dump_anon;



--
-- A. Dump
--

\! ${YB_BUILD_ROOT}/postgres/bin/ysql_dump -h ${PGHOST} --username dump_anon --dbname=yugabyte --no-security-labels > tmp/_pg_dump_A.sql # YB: Use ysql_dump with the correct address and use yugabyte database

--
-- B. Restore
--

DROP SCHEMA test CASCADE;
DROP SEQUENCE public.seq42;

RESET ROLE;
DROP EXTENSION anon;
SET ROLE oscar_the_owner;

\! ${YB_BUILD_ROOT}/postgres/bin/ysqlsh -f tmp/_pg_dump_A.sql yugabyte >/dev/null # YB: Use ysqlsh and yugabyte database


--
-- C. Unmasked data is present
--
SELECT i=1 FROM test.no_masks;

--
-- D. Masked Data is Masked
--
SELECT "IBAN" = md5('0') FROM test."COMPANY";
SELECT brand = 'CONFIDENTIAL' FROM test."COMPANY";
SELECT rn = 1991 # 2093049226 FROM test."COMPANY";

--
-- E. Sequences
--
SELECT pg_catalog.nextval('test.customer_id_seq') = 101; -- YB: sequence caching sets the sequence to 100
SELECT pg_catalog.nextval('public.seq42') = 42;

--
-- F. Sampling
--
/* YB(#4288): TABLESAMPLE is not supported
SELECT count(*) < 100 FROM test.hundred;
*/ -- YB

--
-- G. Remove Anon extension
--
-- WORKS ONLY WITH pg_dump > 14
--\! pg_dump --extension pg_catalog.plpgsql contrib_regression | grep 'CREATE EXTENSION' | grep anon


--
-- H. the --inserts option is supported
--

\! ${YB_BUILD_ROOT}/postgres/bin/ysql_dump -h ${PGHOST} --inserts --username dump_anon --dbname=yugabyte --no-security-labels | grep CONFIDENTIAL # YB: Use ysql_dump with the correct address and use yugabyte database

--  CLEAN
RESET ROLE;
-- YB: the restored test.customer gets its identity from ALTER TABLE ... ADD GENERATED (as
-- ysql_dump writes it), so DROP SCHEMA ... CASCADE prints a table-rewrite NOTICE here
DROP SCHEMA test CASCADE;
DROP SEQUENCE public.seq42;

REASSIGN OWNED BY oscar_the_owner TO postgres;
DROP ROLE oscar_the_owner;

REVOKE ALL ON SCHEMA public FROM dump_anon;
REASSIGN OWNED BY dump_anon TO postgres;
DROP ROLE dump_anon;

DROP EXTENSION anon CASCADE;
