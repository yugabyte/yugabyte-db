\unset ECHO
\i test/setup.sql
SET client_min_messages = warning;

CREATE SCHEMA whatever;
CREATE TABLE whatever.foo ( id serial primary key );

-- Make sure we get test function names.
SET client_min_messages = notice;

CREATE OR REPLACE FUNCTION whatever.testthis() RETURNS SETOF TEXT AS $$
    SELECT collect_tap(
           pass('simple pass'),
           pass('another simple pass')
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.testplpgsql() RETURNS SETOF TEXT AS $$
BEGIN
    RETURN NEXT pass( 'plpgsql simple' );
    RETURN NEXT pass( 'plpgsql simple 2' );
    INSERT INTO whatever.foo VALUES(1);
    RETURN NEXT is( MAX(id), 1, 'Should be a 1 in the test table') FROM whatever.foo;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION whatever.testplpgsqldie() RETURNS SETOF TEXT AS $$
BEGIN
    RETURN NEXT pass( 'plpgsql simple' );
    RETURN NEXT pass( 'plpgsql simple 2' );
    INSERT INTO whatever.foo VALUES(1);
    RETURN NEXT is( MAX(id), 1, 'Should be a 1 in the test table') FROM whatever.foo;
    RAISE EXCEPTION 'This test should die, but not halt execution';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION whatever.testy() RETURNS SETOF TEXT AS $$
    SELECT collect_tap(
        pass('pass'),
        fail('this test intentionally fails')
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.testnada() RETURNS SETOF TEXT AS $$
BEGIN
    -- Do nothing.
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION whatever.testz() RETURNS SETOF TEXT AS $$
    SELECT is( MAX(id), NULL, 'Late test should find nothing in the test table') FROM whatever.foo;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever."test ident"() RETURNS SETOF TEXT AS $$
BEGIN
    RETURN NEXT pass( 'ident' );
    RETURN NEXT pass( 'ident 2' );
END;
$$ LANGUAGE plpgsql;

-- Run the actual tests. Yes, it's a one-liner!
SELECT * FROM runtests('whatever'::name);

ROLLBACK;
