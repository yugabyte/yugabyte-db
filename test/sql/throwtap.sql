\unset ECHO
\i test/setup.sql

SELECT plan(84);
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
    '    died: P0001: todo_end() called without todo_start()'
    || CASE WHEN pg_version_num() < 90200 THEN '' ELSE '
        CONTEXT:
            SQL statement "SELECT * FROM todo_end()"
            PL/pgSQL function lives_ok(text,text) line 14 at EXECUTE'
    || CASE WHEN pg_version_num() >= 90500 THEN '' ELSE ' statement' END END
);

/****************************************************************************/
-- test throws_like().
SELECT * FROM check_test(
    throws_like( 'SELECT * FROM todo_end()', '%end() called without todo%', 'whatever' ),
    true,
    'throws_like(sql, pattern, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_like( 'SELECT * FROM todo_end()', '%end() called without todo%' ),
    true,
    'throws_like(sql, pattern)',
    'Should throw exception like ''%end() called without todo%''',
    ''
);

SELECT * FROM check_test(
    throws_like( 'SELECT * FROM todo_end()', '%huh%', 'whatever' ),
    false,
    'throws_like(sql, pattern, desc) fail',
    'whatever',
    '   error message: ''todo_end() called without todo_start()''
   doesn''t match: ''%huh%'''
);

SELECT * FROM check_test(
    throws_like( 'SELECT 1', '%huh%', 'whatever' ),
    false,
    'throws_like(valid sql, pattern, desc)',
    'whatever',
    '    no exception thrown'
);

/****************************************************************************/
-- test throws_ilike().
SELECT * FROM check_test(
    throws_ilike( 'SELECT * FROM todo_end()', '%END() called without todo%', 'whatever' ),
    true,
    'throws_ilike(sql, pattern, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_ilike( 'SELECT * FROM todo_end()', '%END() called without todo%' ),
    true,
    'throws_ilike(sql, pattern)',
    'Should throw exception like ''%END() called without todo%''',
    ''
);

SELECT * FROM check_test(
    throws_ilike( 'SELECT * FROM todo_end()', '%HUH%', 'whatever' ),
    false,
    'throws_ilike(sql, pattern, desc) fail',
    'whatever',
    '   error message: ''todo_end() called without todo_start()''
   doesn''t match: ''%HUH%'''
);

SELECT * FROM check_test(
    throws_ilike( 'SELECT 1', '%HUH%', 'whatever' ),
    false,
    'throws_ilike(valid sql, pattern, desc)',
    'whatever',
    '    no exception thrown'
);

/****************************************************************************/
-- test throws_matching().
SELECT * FROM check_test(
    throws_matching(
        'SELECT * FROM todo_end()',
        '.*end[(][)] called without todo.+',
        'whatever'
    ),
    true,
    'throws_matching(sql, regex, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_matching(
        'SELECT * FROM todo_end()',
        '.*end[(][)] called without todo.+'
    ),
    true,
    'throws_matching(sql, regex, desc)',
    'Should throw exception matching ''.*end[(][)] called without todo.+''',
    ''
);

SELECT * FROM check_test(
    throws_matching(
        'SELECT * FROM todo_end()',
        'huh.+',
        'whatever'
    ),
    false,
    'throws_matching(sql, regex, desc)',
    'whatever',
    '   error message: ''todo_end() called without todo_start()''
   doesn''t match: ''huh.+'''
);

SELECT * FROM check_test(
    throws_matching(
        'SELECT 1',
        'huh.+',
        'whatever'
    ),
    false,
    'throws_matching(valid sql, regex, desc)',
    'whatever',
    '    no exception thrown'
);

/****************************************************************************/
-- test throws_imatching().
SELECT * FROM check_test(
    throws_imatching(
        'SELECT * FROM todo_end()',
        '.*end[(][)] CALLED without todo.+',
        'whatever'
    ),
    true,
    'throws_imatching(sql, regex, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    throws_imatching(
        'SELECT * FROM todo_end()',
        '.*end[(][)] CALLED without todo.+'
    ),
    true,
    'throws_imatching(sql, regex, desc)',
    'Should throw exception matching ''.*end[(][)] CALLED without todo.+''',
    ''
);

SELECT * FROM check_test(
    throws_imatching(
        'SELECT * FROM todo_end()',
        'HUH.+',
        'whatever'
    ),
    false,
    'throws_imatching(sql, regex, desc)',
    'whatever',
    '   error message: ''todo_end() called without todo_start()''
   doesn''t match: ''HUH.+'''
);

SELECT * FROM check_test(
    throws_imatching(
        'SELECT 1',
        'HUH.+',
        'whatever'
    ),
    false,
    'throws_imatching(valid sql, regex, desc)',
    'whatever',
    '    no exception thrown'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
