BEGIN;

-- Basic Example
CREATE TABLE cluedo ( name TEXT, weapon TEXT, room TEXT);
INSERT INTO cluedo VALUES
('Colonel Mustard','Candlestick', 'Kitchen'),
('Professor Plum', 'Revolver', 'Ballroom'),
('Miss Scarlett', 'Dagger', 'Lounge'),
('Mrs. Peacock', 'Rope', 'Dining Room');
SELECT * FROM cluedo;

-- STEP 1 : Load the extension
CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.load();

-- STEP 2 : Declare the masking rules
SECURITY LABEL FOR anon ON COLUMN cluedo.name 
IS 'MASKED WITH FUNCTION anon.random_last_name()';

SECURITY LABEL FOR anon ON COLUMN cluedo.room 
IS 'MASKED WITH FUNCTION cast(''CONFIDENTIAL'' AS TEXT)';

-- STEP 3 : Dump
SELECT anon.dump();

-- Clean up
DROP EXTENSION anon CASCADE;

ROLLBACK;