BEGIN;

SET ROLE postgres;

-- Create a database owner by an unpriviledge user
CREATE ROLE alice LOGIN;
CREATE DATABASE library TEMPLATE template0 OWNER alice;
SELECT rolsuper FROM pg_roles WHERE rolname='alice';

\c library

CREATE EXTENSION anon CASCADE;
SELECT anon.init();

CREATE TABLE author (
  id INT,
  name TEXT
);

ALTER TABLE author OWNER TO alice;

SET ROLE alice;

CREATE FUNCTION public.attack()
RETURNS TEXT
AS $$
  ALTER ROLE alice SUPERUSER;
  SELECT 'CONFIDENTIAL';
$$
LANGUAGE SQL;

SECURITY LABEL FOR anon ON COLUMN author.name
IS 'MASKED WITH FUNCTION public.attack()';

SET ROLE postgres;

SELECT anon.anonymize_database();

SELECT rolsuper FROM pg_roles WHERE rolname='alice';

ROLLBACK;
