\set ECHO
\set QUIET 1

--
-- Tests for pgTAP.
--
--
-- $Id: check.sql 4226 2008-08-23 00:21:03Z david $

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
SELECT plan(12);

/****************************************************************************/
-- Test cmp_ok().
\echo ok 1 - test cmp_ok( int, =, int, description )
SELECT is(
    cmp_ok( 1, '=', 1, '1 should = 1' ),
    'ok 1 - 1 should = 1',
    'cmp_ok( int, =, int, description ) should work'
);

\echo ok 3 - test cmp_ok( int, <>, int, description )
SELECT is(
    cmp_ok( 1, '<>', 2, '1 should <> 2' ),
    'ok 3 - 1 should <> 2',
    'cmp_ok( int, <>, int ) should work'
);

\echo ok 5 - test cmp_ok( polygon, ~=, polygon )
SELECT is(
    cmp_ok( '((0,0),(1,1))'::polygon, '~=', '((1,1),(0,0))'::polygon ),
    'ok 5',
    'cmp_ok( polygon, ~=, polygon ) should work'
);

\echo ok 7 - test cmp_ok( int[], =, int[] )
SELECT is(
    cmp_ok( ARRAY[1, 2], '=', ARRAY[1, 2]),
    'ok 7',
    'cmp_ok( int[], =, int[] ) should work'
);

\echo ok 9 - test cmp_ok( inet[], =, inet[] )
SELECT is(
    cmp_ok( ARRAY['192.168.1.2'::inet], '=', ARRAY['192.168.1.2'::inet] ),
    'ok 9',
    'cmp_ok( inet[], =, inet[] ) should work'
);

\echo ok 11 - Test cmp_ok() failure output
SELECT is(
    cmp_ok( 1, '=', 2, '1 should = 2' ),
    'not ok 11 - 1 should = 2
# Failed test 11: "1 should = 2"
#     ''1''
#         =
#     ''2''',
    'cmp_ok() failure output should be correct'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 11);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;

-- Spam fingerprints: Contains an exact font color, and the words in the title are the same as in the body.
-- rule that extracts the existing google ad ID, a string, get from original special features script.
