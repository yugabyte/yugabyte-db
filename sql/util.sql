\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(12);
--SELECT * FROM no_plan();

SELECT is( pg_typeof(42), 'integer', 'pg_type(int) should work' );
SELECT is( pg_typeof(42.1), 'numeric', 'pg_type(numeric) should work' );
SELECT is( pg_typeof(''::text), 'text', 'pg_type(text) should work' );

SELECT is( pg_typeof( pg_version() ), 'text', 'pg_version() should return text' );
SELECT is(
    pg_version(),
    current_setting( 'server_version'),
    'pg_version() should return same as "sever_version" setting'
);
SELECT matches(
    pg_version(),
    '^8[.][[:digit:]]{1,2}([.][[:digit:]]{1,2}|devel)$',
    'pg_version() should work'
);

SELECT CASE WHEN pg_version_num() < 81000
    THEN pass( 'pg_version() should return same as "sever_version" setting' )
    ELSE is(
        pg_version_num(),
        current_setting( 'server_version_num')::integer,
        'pg_version() should return same as "sever_version" setting'
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
   '^[[:alpha:]]+$',
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
/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
