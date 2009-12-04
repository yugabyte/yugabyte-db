\unset ECHO
\i test_setup.sql

SELECT plan(23);
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
    '^8[.][[:digit:]]{1,2}([.][[:digit:]]{1,2}|devel|(alpha|beta|rc)[[:digit:]]+)$',
    'pg_version() should work'
);

SELECT CASE WHEN pg_version_num() < 81000
    THEN pass( 'pg_version() should return same as "server_version" setting' )
    ELSE is(
        pg_version_num(),
        current_setting( 'server_version_num')::integer,
        'pg_version() should return same as "server_version" setting'
    )
    END;

SELECT is(
    pg_typeof( pg_version_num() ),
    'integer',
    'pg_version_num() should return integer'
);
SELECT matches(
    pg_version_num()::text,
    '^8[[:digit:]]{4}$',
    'pg_version_num() should be correct'
);

SELECT matches(
   os_name(),
   '^[[:alnum:]]+$',
   'os_name() should output something like an OS name'
);

SELECT is(
    findfuncs('pg_catalog', '^abs$'),
    ARRAY['pg_catalog.abs'],
    'findfincs() should return distinct values'
);

SELECT matches(
    pgtap_version()::text,
    '^0[.][[:digit:]]{2}$',
    'pgtap_version() should work'
);

/****************************************************************************/
-- Test collect_tap().
SELECT is(
    CASE WHEN pg_version_num() >= 80400
    THEN collect_tap('foo', 'bar', 'baz')
    ELSE collect_tap('{foo, bar, baz}')
    END,
    'foo
bar
baz',
    'collect_tap() should simply collect tap'
);

/****************************************************************************/
-- Test display_type().
SELECT is( display_type('int4'::regtype, NULL), 'integer', 'display_type(int4)');
SELECT is( display_type('numeric'::regtype, NULL), 'numeric', 'display_type(numeric)');
SELECT is( display_type('numeric'::regtype, 196612), 'numeric(3,0)', 'display_type(numeric, typmod)');
SELECT is( display_type('"char"'::regtype, NULL), '"char"', 'display_type("char")');
SELECT is( display_type('char'::regtype, NULL), 'character', 'display_type(char)');
SELECT is( display_type('timestamp'::regtype, NULL), 'timestamp without time zone', 'display_type(timestamp)');
SELECT is( display_type('timestamptz'::regtype, NULL), 'timestamp with time zone', 'display_type(timestamptz)');

SELECT is( display_type('foo', 'int4'::regtype, NULL), 'foo.integer', 'display_type(foo, int4)');
SELECT is( display_type('HEY', 'numeric'::regtype, NULL), '"HEY".numeric', 'display_type(HEY, numeric)');
SELECT is( display_type('t z', 'int4'::regtype, NULL), '"t z".integer', 'display_type(t z, int4)');

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
