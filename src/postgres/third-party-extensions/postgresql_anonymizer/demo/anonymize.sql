BEGIN;

-- STEP 0 : Basic Example
CREATE TABLE people ( firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('Sarah','Conor','0609110911');
SELECT * FROM people;

-- STEP 1 : activate the extension and load the data
CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.load();

-- STEP 3 : Declare the masking rules 
SECURITY LABEL FOR anon ON COLUMN people.lastname 
IS 'MASKED WITH FUNCTION anon.random_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone 
IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';

-- STEP 4 : Anonymize data  
SELECT anon.anonymize_database();
SELECT * FROM people;

-- STEP 5 : Clean up
ROLLBACK;
