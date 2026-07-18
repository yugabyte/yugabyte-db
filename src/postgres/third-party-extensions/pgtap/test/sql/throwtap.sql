\unset ECHO
\i test/setup.sql

SELECT plan(97);
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
        CONTEXT:'
    || CASE WHEN pg_version_num() < 90600 THEN '' ELSE '
            PL/pgSQL function todo_end() line 7 at RAISE' END
    || '
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
-- Test ASSERTs
SELECT lives_ok(
    CASE WHEN pg_version_num() < 90500 THEN $exec$
CREATE FUNCTION check_assert(b boolean) RETURNS void LANGUAGE plpgsql AS $body$
BEGIN
    RAISE EXCEPTION 'this code should never be called!';
END
$body$;
$exec$
    ELSE $exec$
CREATE FUNCTION check_assert(b boolean) RETURNS void LANGUAGE plpgsql AS $body$
BEGIN
    ASSERT b IS TRUE, 'assert description';
END
$body$;
$exec$
    END
    , 'Create check_assert function'
);

CREATE FUNCTION test_assert() RETURNS SETOF text LANGUAGE plpgsql AS $body$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 90500 THEN
        FOR tap IN SELECT * FROM check_test(
            throws_ok( 'SELECT check_assert(false)', 'P0004', 'assert description' ),
            true,
            'throws_ok catches assert',
            'threw P0004: assert description',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            throws_ok( 'SELECT check_assert(true)', 'P0004' ),
            false,
            'throws_ok does not accept passing assert',
            'threw P0004',
            '      caught: no exception
      wanted: P0004'
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            lives_ok( 'SELECT check_assert(true)' ),
            true,
            'lives_ok calling check_assert(true)',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        -- Check its diagnostics when there is an exception.
        FOR tap IN SELECT * FROM check_test(
            lives_ok( 'SELECT check_assert(false)' ),
            false,
            'lives_ok with check_assert(false)',
            '',
            '    died: P0004: assert description
        CONTEXT:
            PL/pgSQL function check_assert(boolean) line 3 at ASSERT
            SQL statement "SELECT check_assert(false)"
            PL/pgSQL function lives_ok(text,text) line 14 at EXECUTE
            PL/pgSQL function test_assert() line 38 at FOR over SELECT rows'
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;
    ELSE
        FOR tap IN SELECT * FROM check_test(
            pass(''),
            true,
            'throws_ok catches assert',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail(''),
            false,
            'throws_ok does not accept passing assert',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass(''),
            true,
            'lives_ok calling check_assert(true)',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;

        -- Check its diagnostics when there is an exception.
        FOR tap IN SELECT * FROM check_test(
            fail(''),
            false,
            'lives_ok with check_assert(false)',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
END;
$body$;

SELECT * FROM test_assert();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
