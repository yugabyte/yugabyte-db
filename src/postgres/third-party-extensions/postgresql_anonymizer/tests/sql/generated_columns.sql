BEGIN;

CREATE EXTENSION anon;

CREATE SCHEMA nba;

CREATE TABLE nba.player (
  id SERIAL,
  name TEXT,
  height_cm SMALLINT,
  height_in NUMERIC GENERATED ALWAYS AS (height_cm / 2.54) STORED
);

INSERT INTO nba.player (name,height_cm)
VALUES
  ('Muggsy Bogues',160),
  ('Manute Bol', 231),
  ('Michael Jordan', 198);

SECURITY LABEL FOR anon ON FUNCTION pg_catalog.round(FLOAT) IS 'TRUSTED';

SECURITY LABEL FOR anon ON COLUMN nba.player.id
  IS  'MASKED WITH FUNCTION anon.random_int_between(id*1000,id*1000+999)';

SECURITY LABEL FOR anon ON COLUMN nba.player.height_cm
  IS  'MASKED WITH FUNCTION pg_catalog.round(height_cm - 10)';

--
-- Dynamic masking
--

CREATE ROLE bob LOGIN;

SECURITY LABEL FOR anon ON ROLE bob IS 'MASKED';

GRANT USAGE ON SCHEMA nba TO bob;
GRANT ALL ON ALL TABLES IN SCHEMA nba TO bob;

SET anon.transparent_dynamic_masking TO true;

SET ROLE bob;

SELECT height_in = (198-10)/2.54
FROM nba.player
WHERE name = 'Michael Jordan';

COPY (
  SELECT height_in
  FROM nba.player
  WHERE name = 'Manute Bol'
) TO STDOUT;

RESET ROLE;

SELECT height_in = 198 / 2.54
FROM nba.player
WHERE name = 'Michael Jordan';


--
-- Static Masking
--
SELECT anon.anonymize_table('nba.player');

SELECT height_in = (198-10)/2.54
FROM nba.player
WHERE name = 'Michael Jordan';

ROLLBACK;
