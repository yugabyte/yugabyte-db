CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE people(name TEXT, age INT, zipcode TEXT, score INTEGER);

-- main syntax
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

-- alternative syntax
COMMENT ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.fake_first_name()';

--
SECURITY LABEL FOR anon ON COLUMN people.age
IS 'MASKED WITH FUNCTION anon.random_date()';

-- main syntax
COMMENT ON COLUMN people.zipcode
IS 'MASKED WITH FUNCTION md5(NULL)';

-- Value syntax
SECURITY LABEL FOR anon ON COLUMN people.score
IS 'MASKED WITH VALUE 100';

COMMENT ON COLUMN people.score
IS 'MASKED WITH VALUE NULL';

-- only 4 rules
SELECT count(*) = 4
FROM anon.pg_masking_rules;

-- the main syntax overides the alternative
SELECT count(*)=1
FROM anon.pg_masking_rules
WHERE masking_filter = 'anon.fake_last_name()';

-- pg_masks works too
SELECT count(*)=0
FROM anon.pg_masks
WHERE masking_filter = 'anon.fake_first_name()';

-- Values are overidden too
SELECT count(*)=1
FROM anon.pg_masks
WHERE masking_filter = '100';

-- Clean up
DROP TABLE people CASCADE;

DROP EXTENSION anon CASCADE;
