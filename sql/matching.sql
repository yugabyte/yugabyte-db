\set ECHO
\i test_setup.sql

-- $Id$

SELECT plan(20);

/****************************************************************************/
-- Test matches().
SELECT matches( 'foo'::text, 'o', 'matches() should work' );
SELECT matches( 'foo'::text, '^fo', 'matches() should work with a regex' );
SELECT imatches( 'FOO'::text, '^fo', 'imatches() should work with a regex' );

-- Check matches() diagnostics.
\echo ok 4 - matches() failure
SELECT is( matches( 'foo'::text, '^a' ), 'not ok 4
# Failed test 4
#                   ''foo''
#    doesn''t match: ''^a''', 'Check matches diagnostics' );

-- Check doesnt_match.
SELECT doesnt_match( 'foo'::text, 'a', 'doesnt_match() should work' );
SELECT doesnt_match( 'foo'::text, '^o', 'doesnt_match() should work with a regex' );
SELECT doesnt_imatch( 'foo'::text, '^o', 'doesnt_imatch() should work with a regex' );

-- Check doesnt_match diagnostics.
\echo ok 9 - doesnt_match() failure
SELECT is(
    doesnt_match( 'foo'::text, 'o' ),
    'not ok 9
# Failed test 9
#                   ''foo''
#          matches: ''o''',
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
SELECT is( alike( 'foo'::text, 'a%'::text ), 'not ok 14
# Failed test 14
#                   ''foo''
#    doesn''t match: ''a%''', 'Check alike diagnostics' );

-- Test unalike().
SELECT unalike( 'foo'::text, 'f', 'unalike() should work' );
SELECT unalike( 'foo'::text, 'f%i', 'unalike() should work with a regex' );
SELECT unialike( 'FOO'::text, 'f%i', 'iunalike() should work with a regex' );

-- Check unalike() diagnostics.
\echo ok 19 - unalike() failure
SELECT is( unalike( 'foo'::text, 'f%'::text ), 'not ok 19
# Failed test 19
#                   ''foo''
#          matches: ''f%''', 'Check unalike diagnostics' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 14, 19 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
