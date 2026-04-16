CREATE EXTENSION pg_stat_statements;

--
-- simple and compound statements
--
SET pg_stat_statements.track_utility = TRUE;
SET pg_stat_statements.track_planning = TRUE;

--
-- create / alter user
--
SELECT pg_stat_statements_reset();
CREATE USER foo PASSWORD 'fooooooo';
ALTER USER foo PASSWORD 'foo2';
CREATE ROLE fizzbuzz PASSWORD 'barrr';
ALTER ROLE fizzbuzz PASSWORD 'barrr2';
-- Passwords of all lengths must be redacted.
CREATE ROLE minimalpass PASSWORD '1';
CREATE ROLE smallpass PASSWORD '123';
CREATE ROLE largepass PASSWORD '123456790123456';
-- A NULL password must also be redacted.
ALTER ROLE fizzbuzz PASSWORD NULL;
-- Password must be redacted irrespective of its position in the query.
ALTER ROLE fizzbuzz SUPERUSER PASSWORD 'test' NOLOGIN;

-- Test that pgss handles pl/pgSQL statements well.
CREATE FUNCTION returnArg(variadic NUMERIC[]) RETURNS NUMERIC LANGUAGE plpgsql AS $$
DECLARE
	r INTEGER := $1[1];
BEGIN
	RETURN r;
END;
$$;
SELECT returnArg(10, 2);
SELECT returnArg(1, 4, 7);

SELECT query, calls, rows FROM pg_stat_statements ORDER BY query COLLATE "C";
DROP USER foo;
DROP ROLE fizzbuzz;
DROP ROLE minimalpass;
DROP ROLE smallpass;
DROP ROLE largepass;

DROP EXTENSION pg_stat_statements;
