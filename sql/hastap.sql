\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(90);

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
RESET client_min_messages;

/****************************************************************************/
-- Test has_table().

SELECT * FROM check_test(
    has_table( '__SDFSDFD__' ),
    false,
    'has_table(non-existent table)',
    'Table __SDFSDFD__ should exist',
    ''
);

SELECT * FROM check_test(
    has_table( '__SDFSDFD__', 'lol' ),
    false,
    'has_table(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_table( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_table(sch, non-existent tab, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_table( 'pg_type', 'lol' ),
    true,
    'has_table(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_table( 'pg_catalog', 'pg_type', 'desc' ),
    true,
    'has_table(sch, tab, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_table().

SELECT * FROM check_test(
    hasnt_table( '__SDFSDFD__' ),
    true,
    'hasnt_table(non-existent table)',
    'Table __SDFSDFD__ should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_table( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_table(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_table(sch, non-existent tab, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'pg_type', 'lol' ),
    false,
    'hasnt_table(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'pg_catalog', 'pg_type', 'desc' ),
    false,
    'hasnt_table(sch, tab, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test has_view().

SELECT * FROM check_test(
    has_view( '__SDFSDFD__' ),
    false,
    'has_view(non-existent view)',
    'View __SDFSDFD__ should exist',
    ''
);

SELECT * FROM check_test(
    has_view( '__SDFSDFD__', 'howdy' ),
    false,
    'has_view(non-existent view, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    has_view( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_view(sch, non-existtent view, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_view( 'pg_tables', 'yowza' ),
    true,
    'has_view(view, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    has_view( 'information_schema', 'tables', 'desc' ),
    true,
    'has_view(sch, view, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_view().

SELECT * FROM check_test(
    hasnt_view( '__SDFSDFD__' ),
    true,
    'hasnt_view(non-existent view)',
    'View __SDFSDFD__ should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_view( '__SDFSDFD__', 'howdy' ),
    true,
    'hasnt_view(non-existent view, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_view(sch, non-existtent view, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'pg_tables', 'yowza' ),
    false,
    'hasnt_view(view, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'information_schema', 'tables', 'desc' ),
    false,
    'hasnt_view(sch, view, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test has_column().

SELECT * FROM check_test(
    has_column( '__SDFSDFD__', 'foo' ),
    false,
    'has_column(non-existent tab, col)',
    'Column __SDFSDFD__(foo) should exist',
    ''
);

SELECT * FROM check_test(
    has_column( '__SDFSDFD__', 'bar', 'whatever' ),
    false,
    'has_column(non-existent tab, col, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_column( 'foo', '__SDFSDFD__', 'bar', 'desc' ),
    false,
    'has_column(non-existent sch, tab, col, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_column( 'sometab', 'id' ),
    true,
    'has_column(table, column)',
    'Column sometab(id) should exist',
    ''
);

SELECT * FROM check_test(
    has_column( 'information_schema', 'tables', 'table_name', 'desc' ),
    true,
    'has_column(sch, tab, col, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_column().

SELECT * FROM check_test(
    hasnt_column( '__SDFSDFD__', 'foo' ),
    true,
    'hasnt_column(non-existent tab, col)',
    'Column __SDFSDFD__(foo) should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_column( '__SDFSDFD__', 'bar', 'whatever' ),
    true,
    'hasnt_column(non-existent tab, col, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'foo', '__SDFSDFD__', 'bar', 'desc' ),
    true,
    'hasnt_column(non-existent sch, tab, col, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'sometab', 'id' ),
    false,
    'hasnt_column(table, column)',
    'Column sometab(id) should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'information_schema', 'tables', 'table_name', 'desc' ),
    false,
    'hasnt_column(sch, tab, col, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
