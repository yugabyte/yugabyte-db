CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE people(name TEXT, age INT, zipcode TEXT);

CREATE ROLE batman;

-- This works
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MaSKeD WiTH FuNCTioN anon.fake_last_name()';

-- This is not valid
SECURITY LABEL FOR anon ON COLUMN people.age
IS 'MASKED     WITH    FUNCTION      anon.random_date()';

-- This is correct
SECURITY LABEL FOR anon ON ROLE batman IS 'MasKeD';

-- This is not valid
SECURITY LABEL FOR anon ON ROLE batman IS 'maskeeeed';


-- Clean up
DROP TABLE people CASCADE;
DROP ROLE batman;
DROP EXTENSION anon CASCADE;
