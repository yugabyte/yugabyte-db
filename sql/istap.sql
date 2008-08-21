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
SELECT plan(21);

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
SELECT is( is( 1, 2 ), E'not ok 15\n# Failed test 15\n#         have: 1\n#         want: 2', 'is(1, 2) should work' );

/****************************************************************************/
-- Test isnt().
\echo ok 17 - isnt() success
SELECT is( isnt(1, 2), 'ok 17', 'isnt(1, 2) should work' );
\echo ok 19 - isnt() failure
SELECT is( isnt( 1, 1 ), E'not ok 19\n# Failed test 19\n#     1\n#       <>\n#     1', 'is(1, 2) should work' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 15, 19 );

/****************************************************************************/
-- Try using variables.
\set foo '\'' waffle '\''
\set bar '\'' waffle '\''
SELECT is( :foo::text, :bar::text, 'is() should work with psql variables' );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
