\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(28);
--SELECT * FROM no_plan();

/****************************************************************************/
-- test throws_ok().
SELECT * FROM check_test(
    throws_ok( 'SELECT * FROM todo_end()', 'P0001', 'todo_end() called without todo_start()', 'whatever' ),
    true,
    'four-argument form',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT * FROM todo_end()', 'P0001', 'todo_end() called without todo_start()'),
    true,
    'three-argument errcode',
    'threw P0001: todo_end() called without todo_start()',
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
    throws_ok( 'SELECT * FROM todo_end()', 'todo_end() called without todo_start()', 'whatever'),
    true,
    'three argument errmsg',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_ok( 'SELECT * FROM todo_end()', 'todo_end() called without todo_start()'),
    true,
    'two-argument errmsg',
    'threw todo_end() called without todo_start()',
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
    throws_ok( 'SELECT * FROM todo_end()', 97212 ),
    false,
    'invalid errcode',
    'threw 97212',
    '      caught: P0001: todo_end() called without todo_start()
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
    lives_ok( 'SELECT * FROM todo_end()' ),
    false,
    'lives_ok failure diagnostics',
    '',
    '       died: P0001: todo_end() called without todo_start()'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
