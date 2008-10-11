\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(57);

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
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
    'Column sometab(id) should be NOT NULL',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_not_null( 'sometab', 'name' ),
    false,
    'col_not_null( table, column ) fail',
    'Column sometab(name) should be NOT NULL',
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
    'Column sometab(name) should allow NULL',
    ''
);
-- Make sure failure is correct.
SELECT * FROM check_test(
    col_is_null( 'sometab', 'id' ),
    false,
    'col_is_null( tab, col ) fail',
    'Column sometab(id) should allow NULL',
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
    'Column sometab(name) should be type text',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'TEXT' ),
    true,
    'col_type_is( tab, col, type ) insensitive',
    'Column sometab(name) should be type TEXT',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'int4' ),
    false,
    'col_type_is( tab, col, type ) fail',
    'Column sometab(name) should be type int4',
    '       have: text
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
    '       have: numeric(8,0)
        want: numeric(7)'
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
    '       have: 
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
    col_default_is( 'sometab', 'name', '' ),
    true,
    'col_default_is( tab, col, def )',
    'Column sometab(name) should default to ''''',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
