\unset ECHO
\i test/setup.sql

\set numb_tests 54
SELECT plan(:numb_tests);

-- Replace the internal record of the plan for a few tests.
SELECT is( _set('plan', 4), 4, 'Modify internal plan value');

/****************************************************************************/
-- Test pass().
SELECT pass( 'My pass() passed, w00t!' );

-- Test fail().
\set fail_numb 3
\echo ok :fail_numb - Testing fail()
SELECT is(
    fail('oops'),
    format( E'not ok %1$s - oops\n# Failed test %1$s: "oops"', :fail_numb ),
    'We should get the proper output from fail()'
);

/*
 * NOTE: From this point until we call _set('failed') below we should always
 * have *one* test failure, *BUT* if the tests themselves start failing then
 * you'll have extra failures which will throw off all the successive counts!
 */

-- Check the finish() output with no value.
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you failed 1 test of 4',
    'The output of finish() should reflect the test failure'
);

-- Make sure that false and NULL work as well
SELECT is( _set('plan', 6), 6, 'Increase internal plan value after testing finish' );
SELECT is(
    (SELECT * FROM finish(false) LIMIT 1),
    '# Looks like you failed 1 test of 6',
    'The output of finish(false) should reflect the test failure'
);
SELECT is( _set('plan', 8), 8, 'Increase internal plan value after testing finish' );
SELECT is(
    (SELECT * FROM finish(NULL) LIMIT 1),
    '# Looks like you failed 1 test of 8',
    'The output of finish(NULL) should reflect the test failure'
);

-- Verify that finish(true) works
SELECT is( _set('plan', 10), 10, 'Increase internal plan value after testing finish' );
SELECT throws_ok(
    $$SELECT finish(true)$$,
    '1 test failed of 10',
    'finish(true) should throw an exception'
);

/****************************************************************************/
-- Check num_failed
SELECT is( num_failed(), 1, 'We should have one failure' );
SELECT is( _set('failed', 0), 0, 'Reset internal failure count' );
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

-- Try anyelement form.
SELECT is(diag(6), '# 6', 'diag(int)');
SELECT is(diag(11.2), '# 11.2', 'diag(numeric)');
SELECT is(diag(NOW()), '# ' || NOW(), 'diag(timestamptz)');

-- Try variadic anyarray
CREATE FUNCTION test_variadic() RETURNS SETOF TEXT AS $$
BEGIN
    IF pg_version_num() >= 80400 THEN
        RETURN NEXT is(diag('foo'::text, 'bar', 'baz'), '# foobarbaz', 'variadic text');
        RETURN NEXT is(diag(1::int, 3, 4), '# 134', 'variadic int');
        RETURN NEXT is(diag('foo', 'bar', 'baz'), '# foobarbaz', 'variadic unknown');
    ELSE
        RETURN NEXT pass('variadic text');
        RETURN NEXT pass('variadic int');
        RETURN NEXT pass('variadic unknown');
        RETURN;
    END IF;
END;
$$ LANGUAGE plpgsql;

SELECT * FROM test_variadic();

/****************************************************************************/
-- Check no_plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT * FROM no_plan();
SELECT is( _get('plan'), 0, 'no_plan() should have stored a plan of 0' );

-- Set the plan to a high number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4000), '1..4000', 'Set the plan to 4000' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4000 tests but ran 25',
    'The output of finish() should reflect a high test plan'
);

-- Set the plan to a low number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4), '1..4', 'Set the plan to 4' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4 tests but ran 27',
    'The output of finish() should reflect a low test plan'
);

-- Reset the original plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(:numb_tests), '1..' || :numb_tests, 'Reset the plan' );
SELECT is( _get('plan'), :numb_tests, 'plan() should have stored the test count' );

/****************************************************************************/
-- Test ok()
SELECT * FROM check_test( ok(true), true, 'ok(true)', '', '');
SELECT * FROM check_test( ok(true, ''), true, 'ok(true, '''')', '', '' );
SELECT * FROM check_test( ok(true, 'foo'), true, 'ok(true, ''foo'')', 'foo', '' );

SELECT * FROM check_test( ok(false), false, 'ok(false)', '', '' );
SELECT * FROM check_test( ok(false, ''), false, 'ok(false, '''')', '', '' );
SELECT * FROM check_test( ok(false, 'foo'), false, 'ok(false, ''foo'')', 'foo', '' );
SELECT * FROM check_test( ok(NULL, 'null'), false, 'ok(NULL, ''null'')', 'null', '    (test result was NULL)' );

/****************************************************************************/
-- test multiline description. Second line is effectively diagnostic output.
SELECT * FROM check_test(
    ok( true, 'foo
bar' ),
     true,
     'multiline desc', 'foo', 'bar'
 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
