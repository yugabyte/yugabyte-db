\unset ECHO
\i test/setup.sql

SELECT plan(14);
--SELECT * FROM no_plan();

SELECT is( pg_typeof(42), 'integer', 'pg_type(int) should work' );
SELECT is( pg_typeof(42.1), 'numeric', 'pg_type(numeric) should work' );
SELECT is( pg_typeof(''::text), 'text', 'pg_type(text) should work' );

SELECT is( pg_typeof( pg_version() ), 'text', 'pg_version() should return text' );
SELECT is(
    pg_version(),
    current_setting( 'server_version'),
    'pg_version() should return same as "server_version" setting'
);
SELECT matches(
    pg_version(),
    '(?x)'
    '(?: ^[89][.][[:digit:]]{1,2}([.][[:digit:]]{1,2}|devel|(alpha|beta|rc)[[:digit:]]+) )'
    '|'
    '(?: ^1[[:digit:]] (?: [.] [[:digit:]]{1,2} | devel | (?:alpha|beta|rc)[[:digit:]]+))',
    'pg_version() should work'
);

SELECT CASE WHEN pg_version_num() < 81000
    THEN pass( 'pg_version_num() should return same as "server_version_num" setting' )
    ELSE is(
        pg_version_num(),
        current_setting('server_version_num')::integer,
        'pg_version_num() should return same as "server_version_num" setting'
    )
    END;

SELECT is(
    pg_typeof( pg_version_num() ),
    'integer',
    'pg_version_num() should return integer'
);
SELECT matches(
    pg_version_num()::text,
    '^[89][[:digit:]]{4}$|^1[[:digit:]]{5}$',
    'pg_version_num() should be correct'
);

SELECT matches(
   os_name(),
   '^[[:alnum:]]+$',
   'os_name() should output something like an OS name'
);

SELECT is(
    findfuncs('pg_catalog'::name, '^abs$'),
    ARRAY['pg_catalog.abs'],
    'findfincs() should return distinct values'
);

SELECT cmp_ok(
    pgtap_version(),
    '>=',
    1.0,
    'pgtap_version() should work'
);

/****************************************************************************/
-- Test collect_tap().
SELECT is(
    collect_tap(ARRAY['foo', 'bar', 'baz']),
    'foo
bar
baz',
    'collect_tap(text[]) should simply collect tap'
);

CREATE FUNCTION test_variadic() RETURNS TEXT AS $$
BEGIN
    IF pg_version_num() >= 80400 THEN
        RETURN collect_tap('foo', 'bar', 'baz');
    ELSE
        RETURN collect_tap(ARRAY['foo', 'bar', 'baz']);
    END IF;
END;
$$ LANGUAGE plpgsql;

SELECT is(
    test_variadic(),
    'foo
bar
baz',
    'variadic collect_tap() should simply collect tap'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
