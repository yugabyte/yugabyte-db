\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(28);
--SELECT * FROM no_plan();

/****************************************************************************/
-- test throws_ok().
SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', '22012', 'division by zero', 'whatever' ),
    true,
    'four-argument form',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', '22012', 'division by zero'),
    true,
    'three-argument errcode',
    'threw 22012: division by zero',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', '22012' ),
    true,
    'two-argument errcode',
    'threw 22012'
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', 'division by zero', 'whatever' ),
    true,
    'three argument errmsg',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', 'division by zero' ),
    true,
    'two-argument errmsg',
    'threw division by zero',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0' ),
    true,
    'single-argument form',
    'threw an exception',
    ''
);

-- Check its diagnostics for an invalid error code.
SELECT * FROM check_test(
    throws_ok( 'SELECT 1 / 0', 97212 ),
    false,
    'invalid errcode',
    'threw 97212',
    '      caught: 22012: division by zero
      wanted: 97212'
);

SELECT throws_ok( 'SELECT 1 / 0', NULL, NULL, 'throws_ok(1/0, NULL) should work' );

-- Check its diagnostics no error.

SELECT * FROM check_test(
    throws_ok( 'SELECT 1', NULL ),
    false,
    'throws_ok diagnostics',
    'threw an exception',
    '      caught: no exception
      wanted: an exception'
);

/****************************************************************************/
-- test lives_ok().
SELECT lives_ok( 'SELECT 1', 'lives_ok() should work' );

-- Check its diagnostics when there is an exception.
SELECT * FROM check_test(
    lives_ok( 'SELECT 1 / 0' ),
    false,
    'lives_ok failure diagnostics',
    '',
    ' died: 22012: division by zero'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
