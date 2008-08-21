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

-- Set the test plan.
SELECT plan(20);

/****************************************************************************/
-- Test matches().
SELECT matches( 'foo'::text, 'o', 'matches() should work' );
SELECT matches( 'foo'::text, '^fo', 'matches() should work with a regex' );
SELECT imatches( 'FOO'::text, '^fo', 'imatches() should work with a regex' );

-- Check matches() diagnostics.
\echo ok 4 - matches() failure
SELECT is( matches( 'foo'::text, '^a' ), E'not ok 4\n# Failed test 4\n#                   ''foo''\n#    doesn''t match: ''^a''', 'Check matches diagnostics' );

-- Check doesnt_match.
SELECT doesnt_match( 'foo'::text, 'a', 'doesnt_match() should work' );
SELECT doesnt_match( 'foo'::text, '^o', 'doesnt_match() should work with a regex' );
SELECT doesnt_imatch( 'foo'::text, '^o', 'doesnt_imatch() should work with a regex' );

-- Check doesnt_match diagnostics.
\echo ok 9 - doesnt_match() failure
SELECT is(
    doesnt_match( 'foo'::text, 'o' ),
    E'not ok 9\n# Failed test 9\n#                   ''foo''\n#          matches: ''o''',
    'doesnt_match() should work'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 4, 9 );

/****************************************************************************/
-- Test alike().
SELECT alike( 'foo'::text, 'foo', 'alike() should work' );
SELECT alike( 'foo'::text, 'fo%', 'alike() should work with a regex' );
SELECT ialike( 'FOO'::text, 'fo%', 'ialike() should work with a regex' );

-- Check alike() diagnostics.
\echo ok 14 - alike() failure
SELECT is( alike( 'foo'::text, 'a%'::text ), E'not ok 14\n# Failed test 14\n#                   ''foo''\n#    doesn''t match: ''a%''', 'Check alike diagnostics' );

-- Test unalike().
SELECT unalike( 'foo'::text, 'f', 'unalike() should work' );
SELECT unalike( 'foo'::text, 'f%i', 'unalike() should work with a regex' );
SELECT unialike( 'FOO'::text, 'f%i', 'iunalike() should work with a regex' );

-- Check unalike() diagnostics.
\echo ok 19 - unalike() failure
SELECT is( unalike( 'foo'::text, 'f%'::text ), E'not ok 19\n# Failed test 19\n#                   ''foo''\n#          matches: ''f%''', 'Check unalike diagnostics' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 14, 19 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
