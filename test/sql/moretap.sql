\unset ECHO
\i test/setup.sql

\set numb_tests 46
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
UPDATE  __tcache__ SET value = 0 WHERE label = 'failed';
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
SELECT is( value, 0, 'no_plan() should have stored a plan of 0' )
  FROM __tcache__
 WHERE label = 'plan';

-- Set the plan to a high number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4000), '1..4000', 'Set the plan to 4000' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4000 tests but ran 17',
    'The output of finish() should reflect a high test plan'
);

-- Set the plan to a low number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4), '1..4', 'Set the plan to 4' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4 tests but ran 19',
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
