\unset ECHO
\i test/setup.sql

SELECT plan(72);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)

/****************************************************************************/
-- Test extensions_are().
CREATE FUNCTION public.test_extensions() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 90100 THEN
        EXECUTE $E$
            CREATE SCHEMA someschema;
            CREATE SCHEMA ci_schema;
            CREATE SCHEMA "empty schema";
            CREATE EXTENSION IF NOT EXISTS citext SCHEMA ci_schema;
            CREATE EXTENSION IF NOT EXISTS isn SCHEMA someschema;
            CREATE EXTENSION IF NOT EXISTS ltree SCHEMA someschema;
        $E$;

        FOR tap IN SELECT * FROM check_test(
            extensions_are( 'someschema', ARRAY['isn', 'ltree'], 'Got em' ),
            true,
            'extensions_are(sch, exts, desc)',
            'Got em',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            extensions_are( 'someschema', ARRAY['isn', 'ltree'] ),
            true,
            'extensions_are(sch, exts)',
            'Schema someschema should have the correct extensions',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT* FROM check_test(
            extensions_are( ARRAY['citext', 'isn', 'ltree', 'plpgsql', 'pgtap'], 'Got em' ),
            true,
            'extensions_are(exts, desc)',
            'Got em',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT* FROM check_test(
            extensions_are( ARRAY['citext', 'isn', 'ltree', 'plpgsql', 'pgtap'] ),
            true,
            'extensions_are(exts)',
            'Should have the correct extensions',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT* FROM check_test(
            extensions_are( 'ci_schema', ARRAY['citext'], 'Got em' ),
            true,
            'extensions_are(ci_schema, exts, desc)',
            'Got em',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT* FROM check_test(
            extensions_are( 'empty schema', '{}'::name[] ),
            true,
            'extensions_are(non-sch, exts)',
            'Schema "empty schema" should have the correct extensions',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        /********************************************************************/
        -- Test failures and diagnostics.
        FOR tap IN SELECT* FROM check_test(
            extensions_are( 'someschema', ARRAY['ltree', 'nonesuch'], 'Got em' ),
            false,
            'extensions_are(sch, good/bad, desc)',
            'Got em',
            '    Extra extensions:
        isn
    Missing extensions:
        nonesuch'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT* FROM check_test(
            extensions_are( ARRAY['citext', 'isn', 'ltree', 'pgtap', 'nonesuch'] ),
            false,
            'extensions_are(someexts)',
            'Should have the correct extensions',
    '    Extra extensions:
        plpgsql
    Missing extensions:
        nonesuch'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        /********************************************************************/
        -- Test has_extension().
        -- 8 tests
    
        FOR tap IN SELECT * FROM check_test(
            has_extension( 'ci_schema', 'citext', 'desc' ),
            true,
            'has_extension( schema, name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( 'ci_schema', 'citext'::name ),
            true,
            'has_extension( schema, name )',
            'Extension citext should exist in schema ci_schema',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( 'citext'::name, 'desc' ),
            true,
            'has_extension( name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( 'citext' ),
            true,
            'has_extension( name )',
            'Extension citext should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( 'public'::name, '__NON_EXISTS__'::name, 'desc' ),
            false,
            'has_extension( schema, name, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( 'public'::name, '__NON_EXISTS__'::name ),
            false,
            'has_extension( schema, name ) fail',
            'Extension "__NON_EXISTS__" should exist in schema public',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( '__NON_EXISTS__'::name, 'desc' ),
            false,
            'has_extension( name, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_extension( '__NON_EXISTS__'::name ),
            false,
            'has_extension( name ) fail',
            'Extension "__NON_EXISTS__" should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        /********************************************************************/
        -- Test hasnt_extension().
        -- 8 tests
    
        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'public', '__NON_EXISTS__', 'desc' ),
            true,
            'hasnt_extension( schema, name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'public', '__NON_EXISTS__'::name ),
            true,
            'hasnt_extension( schema, name )',
            'Extension "__NON_EXISTS__" should not exist in schema public',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( '__NON_EXISTS__'::name, 'desc' ),
            true,
            'hasnt_extension( name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( '__NON_EXISTS__' ),
            true,
            'hasnt_extension( name )',
            'Extension "__NON_EXISTS__" should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'ci_schema', 'citext', 'desc' ),
            false,
            'hasnt_extension( schema, name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'ci_schema', 'citext'::name ),
            false,
            'hasnt_extension( schema, name )',
            'Extension citext should not exist in schema ci_schema',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'citext', 'desc' ),
            false,
            'hasnt_extension( name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_extension( 'citext' ),
            false,
            'hasnt_extension( name )',
            'Extension citext should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    ELSE
        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(sch, exts, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(sch, exts)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(exts, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(exts)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(ci_schema, exts, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'extensions_are(non-sch, exts)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'extensions_are(sch, good/bad, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'extensions_are(someexts)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        /********************************************************************/
        -- Test has_extension().
        -- 8 tests
    
        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'has_extension( schema, name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'has_extension( schema, name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'has_extension( name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'has_extension( name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'has_extension( schema, name, desc ) fail',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'has_extension( schema, name ) fail',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'has_extension( name, desc ) fail',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'has_extension( name ) fail',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        /********************************************************************/
        -- Test hasnt_extension().
        -- 8 tests
    
        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'hasnt_extension( schema, name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'hasnt_extension( schema, name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'hasnt_extension( name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
            true,
            'hasnt_extension( name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'hasnt_extension( schema, name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'hasnt_extension( schema, name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'hasnt_extension( name, desc )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
            false,
            'hasnt_extension( name )',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    
    END IF;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;
SELECT * FROM public.test_extensions();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
