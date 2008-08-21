\set ECHO
\set QUIET 1

--
-- Tests for pgTAP.
--
--
-- $Id$

-- Format the output for nice TAP.
\pset format unaligned
\pset tuples_only true
\pset pager

-- Create plpgsql if it's not already there.
SET client_min_messages = fatal;
\set ON_ERROR_STOP off
CREATE LANGUAGE plpgsql;

-- Keep things quiet.
SET client_min_messages = warning;

-- Revert all changes on failure.
\set ON_ERROR_ROLBACK 1
\set ON_ERROR_STOP true

-- Load the TAP functions.
BEGIN;
\i pgtap.sql

-- ## SET search_path TO TAPSCHEMA,public;

-- Set the test plan.
SELECT plan(11);

/****************************************************************************/
-- test throws_ok().
SELECT throws_ok( 'SELECT 1 / 0', '22012', 'throws_ok(1/0) should work' );

-- Check its diagnostics for an invalid error code.
\echo ok 2 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1 / 0', 97212 ),
    E'not ok 2 - threw 97212\n# Failed test 2: "threw 97212"\n#       caught: 22012: division by zero\n#       wanted: 97212',
    'We should get the proper diagnostics from throws_ok()'
);

SELECT throws_ok( 'SELECT 1 / 0', NULL, 'throws_ok(1/0, NULL) should work' );

-- Check its diagnostics no error.
\echo ok 5 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1', NULL ),
    E'not ok 5 - threw an exception\n# Failed test 5: "threw an exception"\n#       caught: no exception\n#       wanted: an exception',
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
    E'not ok 8\n# Failed test 8\n#         died: 22012: division by zero',
    'We should get the proper diagnostics for a lives_ok() failure'
);

UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 8 );
\echo ok 10 - lives_ok is ok

/****************************************************************************/
-- test multiline description.
SELECT is(
    ok( true, E'foo\nbar' ),
    E'ok 10 - foo\n# bar',
    'multiline desriptions should have subsequent lines escaped'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
