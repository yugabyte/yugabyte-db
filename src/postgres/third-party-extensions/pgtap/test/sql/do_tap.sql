\unset ECHO
\i test/setup.sql
SET client_min_messages = notice;

SELECT plan(29);
--SELECT * FROM no_plan();

CREATE OR REPLACE FUNCTION public.testthis() RETURNS SETOF TEXT AS $$
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
    findfuncs('public'::name, '^test', 'this'),
    ARRAY[ 'public."test ident"', 'public.testplpgsql'],
    'findfuncs(public, ^test, this) should work'
);

SELECT is(
    findfuncs('public'::name, '^test'),
    ARRAY[ 'public."test ident"', 'public.testplpgsql', 'public.testthis' ],
    'findfuncs(public, ^test) should work'
);

SELECT is(
    findfuncs('^test', 'this'),
    ARRAY[ 'public."test ident"', 'public.testplpgsql'],
    'findfuncs(^test, this) should work'
);

SELECT is(
    findfuncs('^test'),
    ARRAY[ 'public."test ident"', 'public.testplpgsql', 'public.testthis' ],
    'findfuncs(^test) should work'
);

SELECT is(
    findfuncs('foo'),
    CASE WHEN pg_version_num() < 80300 THEN NULL ELSE '{}'::text[] END,
    'findfuncs(unknown) should find no tests'
);

SELECT * FROM do_tap('public', '^test');
SELECT * FROM do_tap('public'::name);
SELECT * FROM do_tap('^test');
SELECT * FROM do_tap();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
