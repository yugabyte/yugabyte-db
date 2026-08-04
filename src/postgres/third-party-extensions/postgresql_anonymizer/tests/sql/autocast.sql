BEGIN;

CREATE TABLE heroes(
  firtname CHAR(30),
  lastname VARCHAR(60),
  birth DATE
);

INSERT INTO heroes VALUES
('Bruce', 'Wayne', '03-30-1939'),
('Bruce', 'Banner', '09-08-1974 '),
('Peter', 'Parker', '10-08-2001');

CREATE EXTENSION anon CASCADE;

SECURITY LABEL FOR anon ON COLUMN heroes.lastname
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

SECURITY LABEL FOR anon ON COLUMN heroes.birth
IS 'MASKED WITH FUNCTION anon.random_date()';

SELECT anon.start_dynamic_masking();

SELECT pg_typeof(anon.fake_last_name()) = 'text'::REGTYPE;

SELECT pg_typeof(lastname) = 'character varying'::REGTYPE
FROM mask.heroes
LIMIT 1;

SELECT pg_typeof(anon.random_date()) = 'timestamp with time zone'::REGTYPE ;

SELECT pg_typeof(birth) = 'date'::REGTYPE
FROM mask.heroes
LIMIT 1;

ROLLBACK;
