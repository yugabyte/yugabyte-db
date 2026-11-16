--
-- How to declare a masking rule
--

BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;

CREATE TABLE player( id SERIAL, name TEXT, points INT);

INSERT INTO player VALUES
( 1, 'Kareem Abdul-Jabbar',	38387),
( 5, 'Michael Jordan',	32292);

SECURITY LABEL FOR anon ON COLUMN public.player.name
  IS 'MASKED WITH FUNCTION anon.dummy_last_name()';

ROLLBACK;
