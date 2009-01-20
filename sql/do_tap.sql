\unset ECHO
\i test_setup.sql
SET client_min_messages = notice;

-- $Id$

SELECT plan(26);
--SELECT * FROM no_plan();

CREATE OR REPLACE FUNCTION public.test_this() RETURNS SETOF TEXT AS $$
    SELECT pass('simple pass') AS foo
    UNION SELECT pass('another simple pass')
    ORDER BY foo ASC;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION public.testplpgsql() RETURNS SETOF TEXT AS $$
BEGIN
    RETURN NEXT pass( 'plpgsql simple' );
    RETURN NEXT pass( 'plpgsql simple 2' );
    RETURN;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION public."test ident"() RETURNS SETOF TEXT AS $$
BEGIN
    RETURN NEXT pass( 'ident' );
    RETURN NEXT pass( 'ident 2' );
    RETURN;
END;
$$ LANGUAGE plpgsql;

SELECT is(
    findfuncs('public', '^test'),
    ARRAY[ 'public."test ident"', 'public.test_this', 'public.testplpgsql' ],
    'findfuncs(public, ^test) should work'
);

SELECT is(
    findfuncs('^test'),
    ARRAY[ 'public."test ident"', 'public.test_this', 'public.testplpgsql' ],
    'findfuncs(^test) should work'
);

SELECT * FROM do_tap('public', '^test');
SELECT * FROM do_tap('public'::name);
SELECT * FROM do_tap('^test');
SELECT * FROM do_tap();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
