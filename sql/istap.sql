\set ECHO
\i test_setup.sql

-- $Id$

SELECT plan(27);

/****************************************************************************/
-- Test is().
\echo ok 1 - is() success
SELECT is( is(1, 1), 'ok 1', 'isa(1, 1) should work' );
\echo ok 3 - is() success 2
SELECT is( is('x'::text, 'x'::text), 'ok 3', 'is(''x'', ''x'') should work' );
\echo ok 5 - is() success 3
SELECT is( is(1.1, 1.10), 'ok 5', 'is(1.1, 1.10) should work' );
\echo ok 7 - is() success 4
SELECT is( is(1.1, 1.10), 'ok 7', 'is(1.1, 1.10) should work' );
\echo ok 9 - is() success 5
SELECT is( is(true, true), 'ok 9', 'is(true, true) should work' );
\echo ok 11 - is() success 6
SELECT is( is(false, false), 'ok 11', 'is(false, false) should work' );
\echo ok 13 - is() success 7
SELECT is( is(1, 1, 'foo'), 'ok 13 - foo', 'is(1, 1, ''foo'') should work' );
\echo ok 15 - is() failure
SELECT is( is( 1, 2 ), 'not ok 15
# Failed test 15
#         have: 1
#         want: 2', 'is(1, 2) should work' );

/****************************************************************************/
-- Test isnt().
\echo ok 17 - isnt() success
SELECT is( isnt(1, 2), 'ok 17', 'isnt(1, 2) should work' );
\echo ok 19 - isnt() failure
SELECT is( isnt( 1, 1 ), 'not ok 19
# Failed test 19
#     1
#       <>
#     1', 'is(1, 2) should work' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 15, 19 );

/****************************************************************************/
-- Try using variables.
\set foo '\'' waffle '\''
\set bar '\'' waffle '\''
SELECT is( :foo::text, :bar::text, 'is() should work with psql variables' );

/****************************************************************************/
-- Try using NULLs.
\echo ok 22 - is(NULL, NULL) success
SELECT is(
    is( NULL::text, NULL::text, 'NULLs' ),
    'ok 22 - NULLs',
    'is(NULL, NULL) should pass'
);

\echo ok 24 - is(NULL, foo) failure
SELECT is(
    is( NULL::text, 'foo' ),
    'not ok 24
# Failed test 24
#         have: NULL
#         want: foo',
    'is(NULL, foo) should fail'
);

\echo ok 26 - is(foo, NULL) failure
SELECT is(
    is( 'foo', NULL::text ),
    'not ok 26
# Failed test 26
#         have: foo
#         want: NULL',
    'is(foo, NULL) should fail'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 24, 26 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
