BEGIN;

CREATE EXTENSION anon;

CREATE USER test;

--
-- Starting with Postgres 15, unprivileged users are not allowed to create new
-- objects in a database. And for all previous versions, the documentation now
-- recommends to run `REVOKE CREATE ON SCHEMA public FROM PUBLIC;` (see link
-- below )
--
-- With this restriction, the elevation attacks below are not possible. In order
-- to test them on Postgres 15+, we have to restore the creation privilege and
-- check that the extension has its own protections against it.
--
-- https://wiki.postgresql.org/wiki/A_Guide_to_CVE-2018-1058%3A_Protect_Your_Search_Path
--

GRANT ALL ON SCHEMA public TO test;

SET ROLE test;

CREATE OR REPLACE FUNCTION public.elevate()
RETURNS void
AS
$$
BEGIN
    EXECUTE FORMAT('GRANT %I TO test', CURRENT_USER);
    EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE '%', SQLERRM;
END;
$$ LANGUAGE plpgsql;

CREATE TABLE employee (
ssn TEXT
);

SELECT pg_has_role('postgres','MEMBER') IS FALSE;

SECURITY LABEL FOR anon ON COLUMN employee.ssn
IS 'MASKED WITH VALUE NULL';

--
-- This trick is still possible until PostgreSQL 16
-- https://github.com/postgres/postgres/commit/b23cd185fd5410e5204683933f848d4583e34b35
--
SAVEPOINT elevate_via_rule;
CREATE RULE "_RETURN" AS
ON SELECT TO public.employee
DO INSTEAD
SELECT public.elevate()::text as ssn;
ROLLBACK TO elevate_via_rule;

RESET ROLE;

SELECT * FROM mask.employee;

SET ROLE test;

SELECT pg_has_role('postgres','MEMBER') IS FALSE;

ROLLBACK;
