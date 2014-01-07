\unset ECHO
\i test/setup.sql

SELECT plan(47);
--SELECT * from no_plan();

/****************************************************************************/
-- Test is().
SELECT * FROM check_test( is(1, 1), true, 'is(1, 1)', '', '' );
SELECT * FROM check_test( is('x'::text, 'x'::text), true, 'is(''x'', ''x'')', '', '' );
SELECT * FROM check_test( is(1.1, 1.10), true, 'is(1.1, 1.10)', '', '' );
SELECT * FROM check_test( is(true, true), true, 'is(true, true)', '', '' );
SELECT * FROM check_test( is(false, false), true, 'is(false, false)', '', '' );
SELECT * FROM check_test( is(1, 1, 'foo'), true, 'is(1, 1, desc)', 'foo', '' );
SELECT * FROM check_test( is( 1, 2 ), false, 'is(1, 2)', '', '        have: 1
        want: 2');

/****************************************************************************/
-- Test isnt().
SELECT * FROM check_test( isnt(1, 2), true, 'isnt(1, 2)', '', '' );
SELECT * FROM check_test( isnt( 1, 1 ), false, 'isnt(1, 1)', '', '        have: 1
        want: anything else' );

/****************************************************************************/
-- Try using variables.
\set foo '\'' waffle '\''
\set bar '\'' waffle '\''
SELECT is( :foo::text, :bar::text, 'is() should work with psql variables' );

/****************************************************************************/
-- Try using NULLs.

SELECT * FROM check_test(
    is( NULL::text, NULL::text, 'NULLs' ),
    true,
    'is(NULL, NULL)',
    'NULLs',
    ''
);

SELECT * FROM check_test(
    is( NULL::text, 'foo' ),
    false,
    'is(NULL, foo)',
    '',
    '        have: NULL
        want: foo'
);

SELECT * FROM check_test(
    is( 'foo', NULL::text ),
    false,
    'is(foo, NULL)',
    '',
    '        have: foo
        want: NULL'
);

SET client_min_messages = warning;
CREATE TABLE mumble ( id int, name text );
RESET client_min_messages;
INSERT INTO mumble VALUES (1, 'hey');

CREATE FUNCTION test_records() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() < 80100 THEN
        -- Can't do shit with records on 8.0
        RETURN NEXT pass('with records!');
        RETURN NEXT pass( 'is(mumble, row) fail should fail');
        RETURN NEXT pass( 'is(mumble, row) fail should have the proper description');
        RETURN NEXT pass( 'is(mumble, row) fail should have the proper diagnostics');
        RETURN NEXT pass( 'is(mumble, row) fail with NULL should fail');
        RETURN NEXT pass( 'is(mumble, row) fail with NULL should have the proper description');
        RETURN NEXT pass( 'is(mumble, row) fail with NULL should have the proper diagnostics');
        RETURN NEXT pass( 'is(mumble, NULL) should fail');
        RETURN NEXT pass( 'is(mumble, NULL) should have the proper description');
        RETURN NEXT pass( 'is(mumble, NULL) should have the proper diagnostics');
    ELSIF pg_version_num() >= 80400 THEN
        RETURN NEXT is( mumble.*, ROW(1, 'hey')::mumble, 'with records!' )
          FROM mumble;

        -- Before 8.3, have to cast to text.
        FOR tap IN SELECT check_test(
            is( mumble.*, ROW(1, 'HEY')::mumble ),
            false,
            'is(mumble, row) fail',
            '',
            '        have: (1,hey)
        want: (1,HEY)'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT check_test(
            is( mumble.*, ROW(1, NULL)::mumble ),
            false,
            'is(mumble, row) fail with NULL',
            '',
            '        have: (1,hey)
        want: (1,)'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT check_test(
            is( mumble.*, NULL::mumble ),
            false,
            'is(mumble, NULL)',
            '',
            '        have: (1,hey)
        want: NULL'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;
    ELSE
        RETURN NEXT is( textin(record_out(mumble.*)), textin(record_out(ROW(1, 'hey'))), 'with records!' )
          FROM mumble;

        FOR tap IN SELECT check_test(
            is( textin(record_out(mumble.*)), textin(record_out(ROW(1, 'HEY')))),
            false,
            'is(mumble, row) fail',
            '',
            '        have: (1,hey)
        want: (1,HEY)'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT check_test(
            is( textin(record_out(mumble.*)), textin(record_out(ROW(1, NULL))) ),
            false,
            'is(mumble, row) fail with NULL',
            '',
            '        have: (1,hey)
        want: (1,)'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT check_test(
            is( textin(record_out(mumble.*)), NULL::text ),
            false,
            'is(mumble, NULL)',
            '',
            '        have: (1,hey)
        want: NULL'
        ) AS b FROM mumble LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_records();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
