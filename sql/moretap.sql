\unset ECHO
\i test_setup.sql

-- $Id$

\set numb_tests 32
SELECT plan(:numb_tests);

-- Replace the internal record of the plan for a few tests.
UPDATE  __tcache__ SET value = 3 WHERE label = 'plan';

/****************************************************************************/
-- Test pass().
SELECT pass( 'My pass() passed, w00t!' );

-- Test fail().
\set fail_numb 2
\echo ok :fail_numb - Testing fail()
SELECT is(
       fail('oops'),
       'not ok 2 - oops
# Failed test 2: "oops"', 'We should get the proper output from fail()');

-- Check the finish() output.
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you failed 1 test of 3',
    'The output of finish() should reflect the test failure'
);

/****************************************************************************/
-- Check num_failed
SELECT is( num_failed(), 1, 'We should have one failure' );
UPDATE __tresults__ SET ok = true, aok = true WHERE numb = :fail_numb;
SELECT is( num_failed(), 0, 'We should now have no failures' );

/****************************************************************************/
-- Check diag.
SELECT is( diag('foo'), '# foo', 'diag() should work properly' );
SELECT is( diag( 'foo
bar'), '# foo
# bar', 'multiline diag() should work properly' );
SELECT is( diag( 'foo
# bar'), '# foo
# # bar', 'multiline diag() should work properly with existing comments' );

/****************************************************************************/
-- Check no_plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT * FROM no_plan();
SELECT is( value, 0, 'no_plan() should have stored a plan of 0' )
  FROM __tcache__
 WHERE label = 'plan';

-- Set the plan to a high number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4000), '1..4000', 'Set the plan to 4000' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4000 tests but ran 11',
    'The output of finish() should reflect a high test plan'
);

-- Set the plan to a low number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4), '1..4', 'Set the plan to 4' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4 tests but ran 13',
    'The output of finish() should reflect a low test plan'
);

-- Reset the original plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(:numb_tests), '1..' || :numb_tests, 'Reset the plan' );
SELECT is( value, :numb_tests, 'plan() should have stored the test count' )
  FROM __tcache__
 WHERE label = 'plan';

/****************************************************************************/
-- Test ok()
\echo ok 17 - ok() success
SELECT is( ok(true), 'ok 17', 'ok(true) should work' );
\echo ok 19 - ok() success 2
SELECT is( ok(true, ''), 'ok 19', 'ok(true, '''') should work' );
\echo ok 21 - ok() success 3
SELECT is( ok(true, 'foo'), 'ok 21 - foo', 'ok(true, ''foo'') should work' );

\echo ok 23 - ok() failure
SELECT is( ok(false), 'not ok 23
# Failed test 23', 'ok(false) should work' );
\echo ok 25 - ok() failure 2
SELECT is( ok(false, ''), 'not ok 25
# Failed test 25', 'ok(false, '''') should work' );
\echo ok 27 - ok() failure 3
SELECT is( ok(false, 'foo'), 'not ok 27 - foo
# Failed test 27: "foo"', 'ok(false, ''foo'') should work' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 23, 25, 27);

/****************************************************************************/
-- test multiline description.
\echo ok 29 - Multline diagnostics
SELECT is(
    ok( true, 'foo
bar' ),
    'ok 29 - foo
# bar',
    'multiline desriptions should have subsequent lines escaped'
);

/****************************************************************************/
-- test multiline description.
\echo ok 31 - Multiline description
SELECT is(
    ok( true, 'foo
bar' ),
    'ok 31 - foo
# bar',
    'multiline desriptions should have subsequent lines escaped'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
