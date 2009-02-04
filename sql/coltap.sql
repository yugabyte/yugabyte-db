\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(135);
--SELECT * from no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2) DEFAULT NULL,
    myint NUMERIC(8) DEFAULT 24,
    plain INTEGER
);
RESET client_min_messages;

/****************************************************************************/
-- Test col_not_null().
SELECT * FROM check_test(
    col_not_null( 'pg_catalog', 'pg_type', 'typname', 'typname not null' ),
    true,
    'col_not_null( sch, tab, col, desc )',
    'typname not null',
    ''
);

SELECT * FROM check_test(
    col_not_null( 'sometab', 'id', 'blah blah blah' ),
    true,
    'col_not_null( tab, col, desc )',
    'blah blah blah',
    ''
);

SELECT * FROM check_test(
    col_not_null( 'sometab', 'id' ),
    true,
    'col_not_null( table, column )',
    'Column sometab.id should be NOT NULL',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_not_null( 'sometab', 'name' ),
    false,
    'col_not_null( table, column ) fail',
    'Column sometab.name should be NOT NULL',
    ''
);

/****************************************************************************/
-- Test col_is_null().
SELECT * FROM check_test(
    col_is_null( 'public', 'sometab', 'name', 'name is null' ),
    true,
    'col_is_null( sch, tab, col, desc )',
    'name is null',
    ''
);

SELECT * FROM check_test(
    col_is_null( 'sometab', 'name', 'my desc' ),
    true,
    'col_is_null( tab, col, desc )',
    'my desc',
    ''
);

SELECT * FROM check_test(
    col_is_null( 'sometab', 'name' ),
    true,
    'col_is_null( tab, col )',
    'Column sometab.name should allow NULL',
    ''
);
-- Make sure failure is correct.
SELECT * FROM check_test(
    col_is_null( 'sometab', 'id' ),
    false,
    'col_is_null( tab, col ) fail',
    'Column sometab.id should allow NULL',
    ''
);

/****************************************************************************/
-- Test col_type_is().
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'text', 'name is text' ),
    true,
    'col_type_is( sch, tab, col, type, desc )',
    'name is text',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'text', 'yadda yadda yadda' ),
    true,
    'col_type_is( tab, col, type, desc )',
    'yadda yadda yadda',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'text' ),
    true,
    'col_type_is( tab, col, type )',
    'Column sometab.name should be type text',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'TEXT' ),
    true,
    'col_type_is( tab, col, type ) insensitive',
    'Column sometab.name should be type TEXT',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'int4' ),
    false,
    'col_type_is( tab, col, type ) fail',
    'Column sometab.name should be type int4',
    '        have: text
        want: int4'
);

/****************************************************************************/
-- Try col_type_is() with precision.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'numb', 'numeric(10,2)', 'lol' ),
    true,
    'col_type_is with precision',
    'lol',
    ''
);

-- Check its diagnostics.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'myint', 'numeric(7)', 'should be numeric(7)' ),
    false,
    'col_type_is precision fail',
    'should be numeric(7)',
    '        have: numeric(8,0)
        want: numeric(7)'
);

/****************************************************************************/
-- Test col_has_default().
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', 'name', 'desc' ),
    true,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'name', 'desc' ),
    true,
    'col_has_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'name' ),
    true,
    'col_has_default( tab, col )',
    'Column sometab.name should have a default',
    ''
);

-- Check with a column with no default.
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', 'plain', 'desc' ),
    false,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'plain', 'desc' ),
    false,
    'col_has_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'plain' ),
    false,
    'col_has_default( tab, col )',
    'Column sometab.plain should have a default',
    ''
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_has_default( 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_has_default( tab, col, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_has_default( 'sometab', '__asdfasdfs__' ),
    false,
    'col_has_default( tab, col )',
    'Column sometab.__asdfasdfs__ should have a default',
    '    Column sometab.__asdfasdfs__ does not exist'
);

/****************************************************************************/
-- Test col_hasnt_default().
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', 'name', 'desc' ),
    false,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'name', 'desc' ),
    false,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'name' ),
    false,
    'col_hasnt_default( tab, col )',
    'Column sometab.name should not have a default',
    ''
);

-- Check with a column with no default.
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', 'plain', 'desc' ),
    true,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'plain', 'desc' ),
    true,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'plain' ),
    true,
    'col_hasnt_default( tab, col )',
    'Column sometab.plain should not have a default',
    ''
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', '__asdfasdfs__' ),
    false,
    'col_hasnt_default( tab, col )',
    'Column sometab.__asdfasdfs__ should not have a default',
    '    Column sometab.__asdfasdfs__ does not exist'
);

/****************************************************************************/
-- Test col_default_is().

SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'name', ''::text, 'name should default to empty string' ),
    true,
    'col_default_is( sch, tab, col, def, desc )',
    'name should default to empty string',
    ''
);

SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'name', 'foo'::text, 'name should default to ''foo''' ),
    false,
    'col_default_is() fail',
    'name should default to ''foo''',
    '        have: 
        want: foo'
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'name', ''::text, 'name should default to empty string' ),
    true,
    'col_default_is( tab, col, def, desc )',
    'name should default to empty string',
    ''
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'name', ''::text ),
    true,
    'col_default_is( tab, col, def )',
    'Column sometab.name should default to ''''',
    ''
);

-- Make sure it works with a non-text column.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myint', 24 ),
    true,
    'col_default_is( tab, col, int )',
    'Column sometab.myint should default to ''24''',
    ''
);

-- Make sure it works with a NULL default.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'numb', NULL::numeric, 'desc' ),
    true,
    'col_default_is( tab, col, NULL, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'sometab', 'numb', NULL::numeric ),
    true,
    'col_default_is( tab, col, NULL )',
    'Column sometab.numb should default to NULL',
    ''
);

-- Make sure that it fails when there is no default.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'plain', 1::integer, 'desc' ),
    false,
    'col_default_is( tab, col, bogus, desc )',
    'desc',
    '    Column sometab.plain has no default'
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'plain', 1::integer ),
    false,
    'col_default_is( tab, col, bogus )',
    'Column sometab.plain should default to ''1''',
    '    Column sometab.plain has no default'
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', '__asdfasdfs__', NULL::text, 'desc' ),
    false,
    'col_default_is( sch, tab, col, def, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_default_is( 'sometab', '__asdfasdfs__', NULL::text, 'desc' ),
    false,
    'col_default_is( tab, col, def, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_default_is( 'sometab', '__asdfasdfs__', NULL::text ),
    false,
    'col_default_is( tab, col, def )',
    'Column sometab.__asdfasdfs__ should default to NULL',
    '    Column sometab.__asdfasdfs__ does not exist'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
