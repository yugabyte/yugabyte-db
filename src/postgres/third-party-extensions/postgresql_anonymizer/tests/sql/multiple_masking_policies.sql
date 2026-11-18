-- This test relies on the following configuration
--
-- ALTER DATABASE contrib_regression
--   SET anon.masking_policies = 'devtests, analytics';
--

BEGIN;

CREATE EXTENSION anon;

SECURITY LABEL FOR anon ON FUNCTION pg_catalog.floor(NUMERIC) IS 'TRUSTED';
SECURITY LABEL FOR anon ON FUNCTION pg_catalog.random() IS 'TRUSTED';
SECURITY LABEL FOR anon ON FUNCTION pg_catalog.mod(INT,INT) IS 'TRUSTED';

CREATE SCHEMA nba;

CREATE TABLE nba.player(
  id SERIAL,
  name TEXT,
  total_points INT,
  highest_score INT
);

INSERT INTO nba.player VALUES
  ( 1, 'Kareem Abdul-Jabbar', 38387, 55),
  ( 5, 'Michael Jordan', 32292, 69);

-- Devin is a developer. He wants to run CI tests on his code using
-- fake/random data.

CREATE ROLE devin LOGIN;

GRANT USAGE ON SCHEMA nba TO devin;
GRANT SELECT ON ALL TABLES IN SCHEMA nba TO devin;

SECURITY LABEL FOR devtests ON COLUMN nba.player.name
  IS 'MASKED WITH FUNCTION anon.dummy_name()';

SECURITY LABEL FOR devtests ON COLUMN nba.player.total_points
  IS 'MASKED WITH FUNCTION pg_catalog.floor(pg_catalog.random()*40000)';

SECURITY LABEL FOR devtests ON COLUMN nba.player.highest_score
  IS 'MASKED WITH FUNCTION anon.random_int_between(0,50)';

SECURITY LABEL FOR devtests ON ROLE devin IS 'MASKED';

SECURITY LABEL FOR devtests ON FUNCTION anon.random_int_between IS 'TRUSTED';

-- Anna is a Data Scientist. She needs to run global stats over the dataset,
-- she wants to keep the real value on the `highest_score` column but she does
-- not need to know the players names

CREATE ROLE anna LOGIN;

GRANT USAGE ON SCHEMA nba TO anna;
GRANT SELECT ON ALL TABLES IN SCHEMA nba TO anna;

SECURITY LABEL FOR analytics ON COLUMN nba.player.name
  IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR analytics ON ROLE anna IS 'MASKED';

-- We use dynamic masking for testing, but multiple masking policies will
-- also work with static masking and anonymized dumps.

SELECT * FROM nba.player;

SET anon.transparent_dynamic_masking TO true;

-- Devin sees fake data

SET ROLE devin;

--SELECT * FROM nba.player;

SELECT name IS NOT NULL FROM nba.player WHERE id = 5;

SELECT name != 'Michael Jordan'  FROM nba.player WHERE id = 5;

SELECT nba.player.highest_score <= 50 FROM nba.player WHERE id = 5;

RESET ROLE;

-- Anna sees real points but no names

SET ROLE anna;

SELECT * FROM nba.player;

SELECT name IS NULL FROM nba.player WHERE id = 5;

SELECT highest_score = 69 FROM nba.player WHERE id = 5;

RESET ROLE;

-- Static Masking

SAVEPOINT init;

SELECT anon.anonymize_column('nba.player','name','analytics');

-- analytics policy is applied
SELECT nba.player.name IS NULL FROM nba.player WHERE id = 5;

-- devtest policy is not applied
SELECT nba.player.total_points = 38387 FROM nba.player WHERE id = 1;

ROLLBACK TO init;

SELECT anon.anonymize_table('nba.player','analytics');

-- analytics policy is applied
SELECT nba.player.name IS NULL FROM nba.player WHERE id = 5;

-- devtest policy is not applied
SELECT nba.player.total_points = 38387 FROM nba.player WHERE id = 1;

ROLLBACK TO init;

SELECT anon.anonymize_database('analytics');

-- analytics policy is applied
SELECT nba.player.name IS NULL FROM nba.player WHERE id = 5;

-- devtest policy is not applied
SELECT nba.player.total_points = 38387 FROM nba.player WHERE id = 1;

ROLLBACK TO init;


ROLLBACK;
