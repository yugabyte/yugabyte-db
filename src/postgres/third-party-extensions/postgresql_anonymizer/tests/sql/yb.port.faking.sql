/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665

--
-- Personal Data (First Name, etc.)
--

--- Address
SELECT pg_typeof(anon.fake_address()) = 'TEXT'::REGTYPE;

-- First Name
SELECT pg_typeof(anon.fake_first_name()) = 'TEXT'::REGTYPE;

-- Last Name
SELECT pg_typeof(anon.fake_last_name()) = 'TEXT'::REGTYPE;

-- Email
SELECT pg_typeof(anon.fake_email()) = 'TEXT'::REGTYPE;

-- City
SELECT pg_typeof(anon.fake_city()) = 'TEXT'::REGTYPE;

-- Company
SELECT pg_typeof(anon.fake_company()) = 'TEXT'::REGTYPE;

-- Country
SELECT pg_typeof(anon.fake_country()) = 'TEXT'::REGTYPE;

-- IBAN
SELECT pg_typeof(anon.fake_iban()) = 'TEXT'::REGTYPE;

-- postcode
SELECT pg_typeof(anon.fake_postcode()) = 'TEXT'::REGTYPE;

-- SIRET
SELECT pg_typeof(anon.fake_siret()) = 'TEXT'::REGTYPE;

-- Lorem Ipsum
SELECT COUNT(*) = 5-1
FROM (
  SELECT regexp_matches(anon.lorem_ipsum(), E'\n', 'g')
) AS l;

SELECT COUNT(*) = 19-1
FROM (
  SELECT regexp_matches(anon.lorem_ipsum(19), E'\n', 'g')
) AS l;

SELECT COUNT(*) = 7-1
FROM (
  SELECT regexp_matches(anon.lorem_ipsum( paragraphs := 7 ), E'\n', 'g')
) AS l;

SELECT COUNT(*) = 20
FROM unnest(string_to_array( anon.lorem_ipsum( words := 20 ), ' ') )
AS l;

SELECT char_length(anon.lorem_ipsum( characters := 7 )) = 7;

SELECT char_length(anon.lorem_ipsum( characters := 7 , words := 100)) = 7;

SELECT char_length(anon.lorem_ipsum( characters := 7 , paragraphs := 100)) = 7;

-- Issue #223 : fake_* function should not return NULL
TRUNCATE anon.last_name;
INSERT INTO anon.last_name VALUES ( 1,'joan' ), (2,'ken');
SELECT setval('anon.last_name_oid_seq', 2, true);
SELECT bool_and(anon.fake_last_name() IS NOT NULL) FROM generate_series(1,100);


DROP EXTENSION anon CASCADE;

/* YB: workaround for lack of transactional DDL
ROLLBACK;
*/ -- YB
