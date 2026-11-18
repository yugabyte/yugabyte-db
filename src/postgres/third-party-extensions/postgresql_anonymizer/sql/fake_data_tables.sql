-------------------------------------------------------------------------------
-- Fake Data
-------------------------------------------------------------------------------

SET search_path='';

-- Address
DROP TABLE IF EXISTS anon.address;
CREATE TABLE anon.address (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.address CLUSTER ON address_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.address TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.address_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.address IS 'Fake Adresses';

-- Cities
DROP TABLE IF EXISTS anon.city;
CREATE TABLE anon.city (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.city CLUSTER ON city_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.city TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.city_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.city IS 'Fake Cities';

-- Companies
DROP TABLE IF EXISTS anon.company;
CREATE TABLE anon.company (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.company CLUSTER ON company_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.company TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.company_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.city IS 'Fake Companies';

-- Country
DROP TABLE IF EXISTS anon.country;
CREATE TABLE anon.country (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.country CLUSTER ON country_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.country TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.country_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.country IS 'Fake Countries';

-- Email
DROP TABLE IF EXISTS anon.email;
CREATE TABLE anon.email (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.email CLUSTER ON email_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.email TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.email_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.email IS 'Fake email adresses';

-- First names
DROP TABLE IF EXISTS anon.first_name;
CREATE TABLE anon.first_name (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.first_name CLUSTER ON first_name_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.first_name TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.first_name_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.first_name IS 'Fake first names';

-- IBAN
DROP TABLE IF EXISTS anon.iban;
CREATE TABLE anon.iban (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.iban CLUSTER ON iban_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.iban TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.iban_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.iban IS 'Fake IBAN codes';

-- Last names
DROP TABLE IF EXISTS anon.last_name;
CREATE TABLE anon.last_name (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.last_name CLUSTER ON last_name_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.last_name TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.last_name_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.last_name IS 'Fake last names';

-- Postcode
DROP TABLE IF EXISTS anon.postcode;
CREATE TABLE anon.postcode (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.postcode CLUSTER ON postcode_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.postcode TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.postcode_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.postcode IS 'Fake street post codes';

-- SIRET
DROP TABLE IF EXISTS anon.siret;
CREATE TABLE anon.siret (
  oid SERIAL PRIMARY KEY,
  val TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.siret CLUSTER ON siret_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.siret TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.siret_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.siret IS 'Fake SIRET codes';

-- Lorem Ipsum
DROP TABLE IF EXISTS anon.lorem_ipsum;
CREATE TABLE anon.lorem_ipsum (
  oid SERIAL PRIMARY KEY,
  paragraph TEXT
);

/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
ALTER TABLE anon.lorem_ipsum CLUSTER ON lorem_ipsum_pkey;
*/ -- YB
GRANT SELECT ON TABLE anon.lorem_ipsum TO PUBLIC;
GRANT SELECT ON SEQUENCE anon.lorem_ipsum_oid_seq TO PUBLIC;

COMMENT ON TABLE anon.lorem_ipsum IS 'Fake text';

-- ADD NEW TABLE HERE


RESET search_path;
