
-- STEP 1 : Activate the masking engine
CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.start_dynamic_masking();

-- STEP 2 : Declare a masked user
CREATE ROLE skynet LOGIN;
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

-- STEP 3 : Declare the masking rules
CREATE TABLE people ( id TEXT, firstname TEXT, lastname TEXT, phone TEXT);
INSERT INTO people VALUES ('T1','Sarah', 'Conor','0609110911');
SELECT * FROM people;

-- STEP 3 : Declare the masking rules
SECURITY LABEL FOR anon ON COLUMN people.lastname
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR anon ON COLUMN people.phone
IS 'MASKED WITH FUNCTION anon.partial(phone,2,$$******$$,2)';

-- STEP 4 : Connect with the masked user
\! psql demo -U skynet -c 'SELECT * FROM people;'

-- STEP 5 : Clean up
--DROP EXTENSION anon CASCADE;
--REASSIGN OWNED BY skynet TO postgres;
--DROP OWNED BY skynet;
--DROP ROLE skynet;
