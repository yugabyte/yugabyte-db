BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.init();

-- hex_to_int
SELECT anon.hex_to_int(NULL) IS NULL;
SELECT anon.hex_to_int('000000') = 0;
SELECT anon.hex_to_int('123456') = 1193046;
SELECT anon.hex_to_int('ffffff') = 16777215;

-- projection_to_oid
SELECT anon.projection_to_oid(NULL::TIMESTAMP,NULL,NULL) IS NULL;
SELECT anon.projection_to_oid('abcdefgh'::TEXT,'', 10000) = 9096;
SELECT anon.projection_to_oid('xxxxxxxx'::VARCHAR(10),'yyyyy', 10000) = 9784;
SELECT anon.projection_to_oid(42,'',10000);
SELECT anon.projection_to_oid('{}'::JSONB,'',10000);
SELECT anon.projection_to_oid('2020-03-24'::DATE,'',10000);

-- First Name
SELECT  anon.pseudo_first_name(NULL::JSONB) IS NULL;

SELECT  anon.pseudo_first_name('bob'::TEXT)
      = anon.pseudo_first_name('bob'::VARCHAR);

SELECT  anon.pseudo_first_name('bob'::TEXT,'123salt*!')
      = anon.pseudo_first_name('bob'::VARCHAR,'123salt*!');

SELECT anon.pseudo_first_name('42'::TEXT)
     = anon.pseudo_first_name(42);

SELECT pg_typeof(anon.pseudo_first_name(NULL::UUID)) = 'TEXT'::REGTYPE;

-- Last Name
SELECT  anon.pseudo_last_name(NULL::INT) IS NULL;
SELECT  anon.pseudo_last_name('bob'::TEXT,'x')
      = anon.pseudo_last_name('bob'::TEXT,'x');

-- Email
SELECT  anon.pseudo_email(NULL::INT) IS NULL;
SELECT  anon.pseudo_email('bob'::TEXT,'x')
      = anon.pseudo_email('bob'::TEXT,'x');

-- City
SELECT  anon.pseudo_city(NULL::INT) IS NULL;
SELECT  anon.pseudo_city('bob'::TEXT,'x')
      = anon.pseudo_city('bob'::TEXT,'x');

-- Country
SELECT  anon.pseudo_country(NULL::CHAR) IS NULL;
SELECT  anon.pseudo_country('bob'::TEXT,'x')
      = anon.pseudo_country('bob'::TEXT,'x');

-- Company
SELECT  anon.pseudo_company(NULL::POINT) IS NULL;
SELECT  anon.pseudo_company('bob'::TEXT,'x')
      = anon.pseudo_company('bob'::TEXT,'x');

-- IBAN
SELECT  anon.pseudo_iban(NULL::XML) IS NULL;
SELECT  anon.pseudo_iban('bob'::TEXT,'x')
      = anon.pseudo_iban('bob'::TEXT,'x');


-- Use a predefined secret salt

SET anon.salt TO 'a_VeRy_SeCReT_SaLT';

SELECT anon.pseudo_last_name('bob'::TEXT);


ROLLBACK;
