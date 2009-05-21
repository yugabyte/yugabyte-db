\unset ECHO
\i test_setup.sql

SELECT plan(37);

/****************************************************************************/
-- Test is().
SELECT * FROM check_test( is(1, 1), true, 'is(1, 1)', '', '' );
SELECT * FROM check_test( is('x'::text, 'x'::text), true, 'is(''x'', ''x'')', '', '' );
SELECT * FROM check_test( is(1.1, 1.10), true, 'is(1.1, 1.10)', '', '' );
SELECT * FROM check_test( is(true, true), true, 'is(true, true)', '', '' );
SELECT * FROM check_test( is(false, false), true, 'is(false, false)', '', '' );
SELECT * FROM check_test( is(1, 1, 'foo'), true, 'is(1, 1, desc)', 'foo', '' );
SELECT * FROM check_test( is( 1, 2 ), false, 'is(1, 2)', '', '       have: 1
        want: 2');

/****************************************************************************/
-- Test isnt().
SELECT * FROM check_test( isnt(1, 2), true, 'isnt(1, 2)', '', '' );
SELECT * FROM check_test( isnt( 1, 1 ), false, 'isnt(1, 1)', '', '   1
      <>
    1' );

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
    '       have: NULL
        want: foo'
);

SELECT * FROM check_test(
    is( 'foo', NULL::text ),
    false,
    'is(foo, NULL)',
    '',
    '       have: foo
        want: NULL'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
