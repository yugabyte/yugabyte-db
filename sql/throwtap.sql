\set ECHO
\i test_setup.sql

-- $Id$

SELECT plan(11);

/****************************************************************************/
-- test throws_ok().
SELECT throws_ok( 'SELECT 1 / 0', '22012', 'throws_ok(1/0) should work' );

-- Check its diagnostics for an invalid error code.
\echo ok 2 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1 / 0', 97212 ),
    'not ok 2 - threw 97212
# Failed test 2: "threw 97212"
#       caught: 22012: division by zero
#       wanted: 97212',
    'We should get the proper diagnostics from throws_ok()'
);

SELECT throws_ok( 'SELECT 1 / 0', NULL, 'throws_ok(1/0, NULL) should work' );

-- Check its diagnostics no error.
\echo ok 5 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1', NULL ),
    'not ok 5 - threw an exception
# Failed test 5: "threw an exception"
#       caught: no exception
#       wanted: an exception',
    'We should get the proper diagnostics from throws_ok() with a NULL error code'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 2, 5 );

/****************************************************************************/
-- test lives_ok().
SELECT lives_ok( 'SELECT 1', 'lives_ok() should work' );

-- Check its diagnostics when there is an exception.
\echo ok 8 - lives_ok failure diagnostics
SELECT is(
    lives_ok( 'SELECT 1 / 0' ),
    'not ok 8
# Failed test 8
#         died: 22012: division by zero',
    'We should get the proper diagnostics for a lives_ok() failure'
);

UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 8 );
\echo ok 10 - lives_ok is ok

/****************************************************************************/
-- test multiline description.
SELECT is(
    ok( true, 'foo
bar' ),
    'ok 10 - foo
# bar',
    'multiline desriptions should have subsequent lines escaped'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
