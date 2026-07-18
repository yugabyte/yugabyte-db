BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

CREATE TABLE player( id SERIAL, name TEXT, points INT);

INSERT INTO player VALUES  
( 1, 'Kareem Abdul-Jabbar',	38387),
( 5, 'Michael Jordan',	32292);

SECURITY LABEL FOR anon ON COLUMN player.name 
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

ROLLBACK;