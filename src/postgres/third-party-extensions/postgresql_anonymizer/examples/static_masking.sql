--
-- This is a basic example of static masking
--

BEGIN;

-- STEP 1: Create the table

CREATE TABLE customer(
	id SERIAL,
	full_name TEXT,
	birth DATE,
	employer TEXT,
	zipcode TEXT,
	fk_shop INTEGER
);

INSERT INTO customer
VALUES
(911,'Chuck Norris','1940-03-10','Texas Rangers', '75001',12),
(312,'David Hasselhoff','1952-07-17','Baywatch', '90001',423)
;

SELECT * FROM customer;

-- STEP 2: Load the extension

CREATE EXTENSION IF NOT EXISTS anon;

-- STEP 3: Declare the masking rules

SECURITY LABEL FOR anon ON COLUMN customer.full_name
IS 'MASKED WITH FUNCTION anon.dummy_first_name() || '' '' || anon.dummy_last_name()';

SECURITY LABEL FOR anon ON COLUMN customer.employer
IS 'MASKED WITH FUNCTION anon.dummy_company_name()';

SECURITY LABEL FOR anon ON COLUMN customer.zipcode
IS 'MASKED WITH FUNCTION anon.random_zip()';

-- STEP 4: Replace Sensitive Data

SELECT anon.anonymize_database();

SELECT * FROM customer;

ROLLBACK;
