\unset ECHO
\i test/setup.sql
SET client_min_messages = warning;

CREATE SCHEMA whatever;

CREATE OR REPLACE FUNCTION whatever.testthis() RETURNS SETOF TEXT AS $$
BEGIN
    -- Do nothing.
END;
$$ LANGUAGE plpgsql;

SELECT * FROM runtests('whatever'::name);
ROLLBACK;
