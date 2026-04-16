\unset ECHO
\i test/setup.sql
SET client_min_messages = warning;

CREATE SCHEMA whatever;
CREATE TABLE whatever.foo ( id serial primary key );

/*

Expected output:

runtests.out:   9.6 and up
runtests_1.out: 9.5
runtests_2.out: 9.3 - 9.4
runtests_3.out: 9.2
runtests_4.out: 9.1

*/

-- Make sure we get test function names.
SET client_min_messages = notice;

CREATE OR REPLACE FUNCTION whatever.startup() RETURNS SETOF TEXT AS $$
    SELECT pass('starting up');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.startupmore() RETURNS SETOF TEXT AS $$
    SELECT pass('starting up some more');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.setup() RETURNS SETOF TEXT AS $$
    SELECT collect_tap(ARRAY[
        pass('setup'),
        (SELECT is( MAX(id), NULL, 'Should be nothing in the test table') FROM whatever.foo)
    ]);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.setupmore() RETURNS SETOF TEXT AS $$
    SELECT pass('setup more');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.teardown() RETURNS SETOF TEXT AS $$
    SELECT pass('teardown');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.teardownmore() RETURNS SETOF TEXT AS $$
    SELECT pass('teardown more');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.shutdown() RETURNS SETOF TEXT AS $$
    SELECT pass('shutting down');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.shutdownmore() RETURNS SETOF TEXT AS $$
    SELECT pass('shutting down more');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION whatever.testthis() RETURNS SETOF TEXT AS $$
    SELECT collect_tap(ARRAY[
           pass('simple pass'),
           pass('another simple pass')
    ]);
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
    RETURN NEXT pass( 'plpgsql simple' );   -- Won't appear in results.
    RETURN NEXT pass( 'plpgsql simple 2' ); -- Won't appear in results.
    INSERT INTO whatever.foo VALUES(1);
    RETURN NEXT is( MAX(id), 1, 'Should be a 1 in the test table') FROM whatever.foo;
    IF pg_version_num() >= 90300 THEN
        EXECUTE $E$
            CREATE OR REPLACE FUNCTION __die() RETURNS VOID LANGUAGE plpgsql AS $F$
            BEGIN
            RAISE EXCEPTION 'This test should die, but not halt execution.
Note that in some cases we get what appears to be a duplicate context message, but that is due to Postgres itself.'
            USING
                    DETAIL =      'DETAIL',
                    COLUMN =      'COLUMN',
                    CONSTRAINT =  'CONSTRAINT',
                    DATATYPE =    'TYPE',
                    TABLE =       'TABLE',
                    SCHEMA =      'SCHEMA';
            END;
            $F$;
        $E$;
        EXECUTE 'SELECT __die();';
    ELSIF pg_version_num() >= 80400 THEN
        EXECUTE $E$
            CREATE OR REPLACE FUNCTION __die() RETURNS VOID LANGUAGE plpgsql AS $F$
            BEGIN
            RAISE EXCEPTION 'This test should die, but not halt execution.
Note that in some cases we get what appears to be a duplicate context message, but that is due to Postgres itself.'
                USING DETAIL = 'DETAIL';
            END;
            $F$;
        $E$;
        EXECUTE 'SELECT __die();';
    ELSE
        RAISE EXCEPTION 'This test should die, but not halt execution';
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION whatever.testdividebyzero() RETURNS SETOF TEXT AS $$
    select cast(1/0 as text)
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION whatever.testy() RETURNS SETOF TEXT AS $$
    SELECT fail('this test intentionally fails');
$$ LANGUAGE SQL;

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

-- Verify that startup, shutdown, etc aren't run as normal tests
SELECT * FROM runtests('whatever'::name, '.*') WHERE pg_version_num() >= 80300;

ROLLBACK;
