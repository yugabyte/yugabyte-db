CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

--
-- Generic Types
--

-- zip
SELECT pg_typeof(anon.random_zip()) = 'TEXT'::REGTYPE;


-- string

SELECT pg_typeof(anon.random_string(1)) = 'TEXT'::REGTYPE;
--SELECT anon_string(123456789);


-- Date
SELECT pg_typeof(anon.random_date_between('1900-01-01'::TIMESTAMP WITH TIME ZONE,now())) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;
SELECT pg_typeof(anon.random_date_between('0001-01-01'::DATE,'4001-01-01'::DATE)) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;
SELECT pg_typeof(anon.random_date()) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;

-- Integer
SELECT pg_typeof(anon.random_int_between(1,3)) = 'INTEGER'::REGTYPE;

--
-- Personal Data (First Name, etc.)
--

-- First Name
SELECT pg_typeof(anon.fake_first_name()) = 'TEXT'::REGTYPE;

-- Last Name
SELECT pg_typeof(anon.fake_last_name()) = 'TEXT'::REGTYPE;


-- Email
SELECT pg_typeof(anon.fake_email()) = 'TEXT'::REGTYPE;

-- Phone
SELECT pg_typeof(anon.random_phone('0033')) = 'TEXT'::REGTYPE;
SELECT anon.random_phone(NULL) IS NULL;
SELECT pg_typeof(anon.random_phone()) = 'TEXT'::REGTYPE;

-- Location
SELECT pg_typeof(anon.random_city_in_country('France')) = 'TEXT'::REGTYPE;
SELECT anon.random_city_in_country('dfndjndjnjdnvjdnjvndjnvjdnvjdnjnvdnvjdnvj') IS NULL;
SELECT anon.random_city_in_country(NULL) IS NULL;
SELECT pg_typeof(anon.random_city()) = 'TEXT'::REGTYPE;
SELECT pg_typeof(anon.random_region_in_country('Italy')) = 'TEXT'::REGTYPE;
SELECT anon.random_region_in_country('c,dksv,kdfsdnfvsjdnfjsdnjfndj') IS NULL;
SELECT anon.random_region_in_country(NULL) IS NULL;
SELECT pg_typeof(anon.random_region()) = 'TEXT'::REGTYPE;
SELECT pg_typeof(anon.random_country()) = 'TEXT'::REGTYPE;


--
-- Company
--
SELECT pg_typeof(anon.fake_company()) = 'TEXT'::REGTYPE;

--
-- IBAN
--
SELECT pg_typeof(anon.fake_iban()) = 'TEXT'::REGTYPE;

--
-- SIRET
--
SELECT pg_typeof(anon.fake_siret()) = 'TEXT'::REGTYPE;
SELECT pg_typeof(anon.fake_siren()) = 'TEXT'::REGTYPE;


-------------------------------------------------------------------------------
-- Masking
-------------------------------------------------------------------------------

CREATE TEMPORARY TABLE t1 (
	id SERIAL,
	name TEXT,
	"CreditCard" TEXT,
	fk_company INTEGER
);

INSERT INTO t1
VALUES (1,'Schwarzenegger','1234567812345678', 1991);


COMMENT ON COLUMN t1.name IS '  MASKED WITH (   FUNCTION = random_last_name() )';
COMMENT ON COLUMN t1."CreditCard" IS '  MASKED WITH (   FUNCTION = random_string(12) )';

CREATE TEMPORARY TABLE "T2" (
	id_company SERIAL,
	"IBAN" TEXT,
	COMPANY TEXT
);

INSERT INTO "T2"
VALUES (1991,'12345677890','Skynet');

COMMENT ON COLUMN "T2"."IBAN" IS 'MASKED WITH (FUNCTION=random_iban())';
COMMENT ON COLUMN "T2".COMPANY IS 'jvnosdfnvsjdnvfskngvknfvg MASKED WITH (FUNCTION = random_company() )  jenfksnvjdnvkjsnvsndvjs';


SELECT count(*) = 4  FROM anon.mask;

SELECT func = 'random_iban()' FROM anon.mask WHERE attname = 'IBAN';

SELECT anon.anonymize_all_the_things();

SELECT company != 'skynet' FROM "T2" WHERE id_company=1991;

SELECT name != 'Schwarzenegger' FROM t1 WHERE id = 1;

-------------------------------------------------------------------------------
-- End
-------------------------------------------------------------------------------

SELECT anon.unload();

DROP EXTENSION anon;
