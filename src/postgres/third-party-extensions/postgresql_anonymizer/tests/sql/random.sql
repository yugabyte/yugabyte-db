BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.init();

--
-- Generic Types
--

-- zip
SELECT pg_typeof(anon.random_zip()) = 'TEXT'::REGTYPE;


-- string
SELECT pg_typeof(anon.random_string(1)) = 'TEXT'::REGTYPE;

-- Range
SELECT anon.random_in_int4range('[2001,2002)') = 2001;
SELECT anon.random_in_int4range('(2001,2002)') IS NULL;
SELECT anon.random_in_int4range('[2001,)') IS NULL;
SELECT anon.random_in_int4range('[,2001]') IS NULL;
SELECT anon.random_in_int4range(NULL) IS NULL;
SELECT bool_and(anon.random_in_int4range('[1,10)') < 10)
FROM generate_series(1,100);

SELECT anon.random_in_daterange('[2021-01-01, 2021-01-02)') = '2021-01-01'::DATE;
SELECT anon.random_in_int8range('[2001,2002)') = 2001;
SELECT anon.random_in_numrange('[0.1,0.9]') < 1;
SELECT extract( DAY FROM anon.random_in_tsrange('[2022-10-9,2022-10-31)')) > 8;
SELECT extract( YEAR FROM anon.random_in_tstzrange('[2022-01-01,2022-12-31]')) = 2022;

-- Date
SELECT pg_typeof(anon.random_date_between('1900-01-01'::TIMESTAMP WITH TIME ZONE,now())) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;
SELECT pg_typeof(anon.random_date_between('0001-01-01'::DATE,'4001-01-01'::DATE)) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;
SELECT pg_typeof(anon.random_date()) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;

-- Integer
SELECT pg_typeof(anon.random_int_between(1,3)) = 'INTEGER'::REGTYPE;
SELECT ROUND(AVG(anon.random_int_between(1,3))) = 2
FROM generate_series(1,100);

SELECT pg_typeof(anon.random_bigint_between(1,3)) = 'BIGINT'::REGTYPE;
SELECT ROUND(AVG(anon.random_bigint_between(2147483648,2147483650))) = 2147483649
FROM generate_series(1,100);


-- Phone
SELECT pg_typeof(anon.random_phone('0033')) = 'TEXT'::REGTYPE;
SELECT anon.random_phone(NULL) IS NULL;
SELECT pg_typeof(anon.random_phone()) = 'TEXT'::REGTYPE;

-- Array
SELECT anon.random_in(NULL::DATE[]) IS NULL;
SELECT avg(anon.random_in(ARRAY[1,2,3]))::INT = 2 FROM generate_series(1,100);
SELECT pg_typeof(anon.random_in(ARRAY['yes','no','maybe'])) = 'TEXT'::REGTYPE;

-- ENUM
CREATE TYPE CARD AS ENUM ('visa', 'mastercard', 'amex');
SELECT pg_typeof(anon.random_in(enum_range(null::CARD))) = 'CARD'::REGTYPE;
SELECT pg_typeof(anon.random_in_enum(NULL::CARD)) = 'CARD'::REGTYPE;


DROP EXTENSION anon CASCADE;

ROLLBACK;
