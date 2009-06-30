\unset ECHO
\i test_setup.sql

SELECT plan(36);
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

-- Try using a prepared statement.
PREPARE mytest AS SELECT * FROM todo_end();
SELECT * FROM check_test(
    throws_ok( 'mytest', 'P0001'),
    true,
    'prepared statement & errcode',
    'threw P0001'
    ''
);

SELECT * FROM check_test(
    throws_ok( 'EXECUTE mytest', 'P0001'),
    true,
    'execute & errcode',
    'threw P0001'
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

PREPARE livetest AS SELECT 1;
SELECT * FROM check_test(
    lives_ok( 'livetest'),
    true,
    'lives_ok(prepared)'
    '',
    ''
);

SELECT * FROM check_test(
    lives_ok( 'EXECUTE livetest'),
    true,
    'lives_ok(execute)'
    '',
    ''
);

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
