\unset ECHO
\i test/setup.sql

SELECT plan(24);

/****************************************************************************/
-- Test matches().
SELECT matches( 'foo'::text, 'o', 'matches() should work' );
SELECT matches( 'foo'::text, '^fo', 'matches() should work with a regex' );
SELECT imatches( 'FOO'::text, '^fo', 'imatches() should work with a regex' );

-- Check matches() diagnostics.
SELECT * FROM check_test(
    matches( 'foo'::text, '^a' ),
    false,
    'matches() fail',
    '',
    '                  ''foo''
   doesn''t match: ''^a'''
);

-- Check doesnt_match.
SELECT doesnt_match( 'foo'::text, 'a', 'doesnt_match() should work' );
SELECT doesnt_match( 'foo'::text, '^o', 'doesnt_match() should work with a regex' );
SELECT doesnt_imatch( 'foo'::text, '^o', 'doesnt_imatch() should work with a regex' );

-- Check doesnt_match diagnostics.
SELECT * FROM check_test(
    doesnt_match( 'foo'::text, 'o' ),
    false,
    'doesnt_match() fail',
    '',
    '                  ''foo''
         matches: ''o'''
);

/****************************************************************************/
-- Test alike().
SELECT alike( 'foo'::text, 'foo', 'alike() should work' );
SELECT alike( 'foo'::text, 'fo%', 'alike() should work with a regex' );
SELECT ialike( 'FOO'::text, 'fo%', 'ialike() should work with a regex' );

-- Check alike() diagnostics.
SELECT * FROM check_test(
    alike( 'foo'::text, 'a%'::text ),
    false,
    'alike() fail',
    '',
    '                  ''foo''
   doesn''t match: ''a%'''
);

-- Test unalike().
SELECT unalike( 'foo'::text, 'f', 'unalike() should work' );
SELECT unalike( 'foo'::text, 'f%i', 'unalike() should work with a regex' );
SELECT unialike( 'FOO'::text, 'f%i', 'iunalike() should work with a regex' );

-- Check unalike() diagnostics.
SELECT * FROM check_test(
    unalike( 'foo'::text, 'f%'::text ),
    false,
    'unalike() fail',
    '',
    '                  ''foo''
         matches: ''f%'''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
