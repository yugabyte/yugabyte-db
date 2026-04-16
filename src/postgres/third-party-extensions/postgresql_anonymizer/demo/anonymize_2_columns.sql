--
-- Currently the masking rules are only applied on a single column, it is not
-- possible to define a rule that would mask 2 columns simultaneously.
--
-- However there are 2 workarounds for this :
--
-- * using GENERATED COLUMNS
-- * using pseudonymization
--

BEGIN;

CREATE TABLE people (
  id SERIAL,
  firstname TEXT,
  firstname_lower TEXT,
  firstname_lower_auto TEXT GENERATED ALWAYS AS (lower(firstname)) STORED,
  lastname TEXT,
  phone TEXT
);

INSERT INTO people(id,firstname,firstname_lower,lastname,phone)
  VALUES (589346,'Sarah','sarah','Conor','0609110911');

SELECT * FROM people;

-- Activate the extension and load the data
CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.init();

-- Masking rules
SECURITY LABEL FOR anon
  ON COLUMN people.firstname
  IS 'MASKED WITH FUNCTION anon.pseudo_first_name(id::TEXT)';

SECURITY LABEL FOR anon
  ON COLUMN people.firstname_lower
  IS 'MASKED WITH FUNCTION lower(anon.pseudo_first_name(id::TEXT))';

-- Anonymize data
SELECT anon.anonymize_database();
SELECT * FROM people;

-- Clean up
ROLLBACK;
