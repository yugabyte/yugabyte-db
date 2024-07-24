\unset ECHO
\i test/setup.sql
-- \i sql/pgtap.sql

SELECT plan(96);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
-- This table has no pk
CREATE TABLE public.pkless(
    id INT NOT NULL UNIQUE
);
CREATE SCHEMA hide;
CREATE TABLE hide.hidesometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);

RESET client_min_messages;

/****************************************************************************/
-- Test has_pk().

SELECT * FROM check_test(
    has_pk( 'public', 'sometab', 'public.sometab should have a pk' ),
    true,
    'has_pk( schema, table, description )',
    'public.sometab should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'public', 'sometab'::name ),
    true,
    'has_pk( schema, table )',
    'Table public.sometab should have a primary key',
    ''
);

SELECT * FROM check_test(
    has_pk( 'hide', 'hidesometab', 'hide.sometab should have a pk' ),
    true,
    'has_pk( hideschema, hidetable, description )',
    'hide.sometab should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'hide', 'hidesometab'::name ),
    true,
    'has_pk( hideschema, hidetable )',
    'Table hide.hidesometab should have a primary key',
    ''
);

SELECT * FROM check_test(
    has_pk( 'sometab', 'sometab should have a pk' ),
    true,
    'has_pk( table, description )',
    'sometab should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'hidesometab', 'hidesometab should have a pk' ),
    false,
    'has_pk( hidetable, description ) fail',
    'hidesometab should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'sometab' ),
    true,
    'has_pk( table )',
    'Table sometab should have a primary key',
    ''
);

SELECT * FROM check_test(
    has_pk( 'public', 'pkless', 'public.pkless should have a pk' ),
    false,
    'has_pk( schema, table, description ) fail',
    'public.pkless should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'pkless', 'pkless should have a pk' ),
    false,
    'has_pk( table, description ) fail',
    'pkless should have a pk',
    ''
);

/****************************************************************************/
-- Test hasnt_pk().

SELECT * FROM check_test(
    hasnt_pk( 'public', 'sometab', 'public.sometab should not have a pk' ),
    false,
    'hasnt_pk( schema, table, description )',
    'public.sometab should not have a pk',
    ''
);

SELECT * FROM check_test(
    hasnt_pk( 'sometab', 'sometab should not have a pk' ),
    false,
    'hasnt_pk( table, description )',
    'sometab should not have a pk',
    ''
);

SELECT * FROM check_test(
    hasnt_pk( 'sometab' ),
    false,
    'hasnt_pk( table )',
    'Table sometab should not have a primary key',
    ''
);

SELECT * FROM check_test(
    hasnt_pk( 'public', 'pkless', 'public.pkless should not have a pk' ),
    true,
    'hasnt_pk( schema, table, description ) pass',
    'public.pkless should not have a pk',
    ''
);

SELECT * FROM check_test(
    hasnt_pk( 'pkless', 'pkless should not have a pk' ),
    true,
    'hasnt_pk( table, description ) pass',
    'pkless should not have a pk',
    ''
);

/****************************************************************************/
-- Test col_is_pk().

SELECT * FROM check_test(
    col_is_pk( 'public', 'sometab', 'id', 'public.sometab.id should be a pk' ),
    true,
    'col_is_pk( schema, table, column, description )',
    'public.sometab.id should be a pk',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'public', 'sometab', 'id'::name ),
    true,
    'col_is_pk( schema, table, column )',
    'Column public.sometab(id) should be a primary key',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'sometab', 'id', 'sometab.id should be a pk' ),
    true,
    'col_is_pk( table, column, description )',
    'sometab.id should be a pk',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'sometab', 'id' ),
    true,
    'col_is_pk( table, column )',
    'Column sometab(id) should be a primary key',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'public', 'sometab', 'name', 'public.sometab.name should be a pk' ),
    false,
    'col_is_pk( schema, table, column, description ) fail',
    'public.sometab.name should be a pk',
    '        have: {id}
        want: {name}'
);

SELECT * FROM check_test(
    col_is_pk( 'sometab', 'name', 'sometab.name should be a pk' ),
    false,
    'col_is_pk( table, column, description ) fail',
    'sometab.name should be a pk',
    '        have: {id}
        want: {name}'
);

/****************************************************************************/
-- Test col_is_pk() with an array of columns.

SET client_min_messages = warning;
CREATE TABLE public.argh (
    id INT NOT NULL,
    name TEXT NOT NULL,
    PRIMARY KEY (id, name)
);
RESET client_min_messages;

SELECT * FROM check_test(
    col_is_pk( 'public', 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    true,
    'col_is_pk( schema, table, column[], description )',
    'id + name should be a pk',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'public', 'argh', ARRAY['id', 'name']::name[] ),
    true,
    'col_is_pk( schema, table, column[] )',
    'Columns public.argh(id, name) should be a primary key',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    true,
    'col_is_pk( table, column[], description )',
    'id + name should be a pk',
    ''
);

SELECT * FROM check_test(
    col_is_pk( 'argh', ARRAY['id', 'name'] ),
    true,
    'col_is_pk( table, column[] )',
    'Columns argh(id, name) should be a primary key',
    ''
);

/****************************************************************************/
-- Test col_isnt_pk().

SELECT * FROM check_test(
    col_isnt_pk( 'public', 'sometab', 'id', 'public.sometab.id should not be a pk' ),
    false,
    'col_isnt_pk( schema, table, column, description )',
    'public.sometab.id should not be a pk',
    '        have: {id}
        want: anything else'
);

SELECT * FROM check_test(
    col_isnt_pk( 'sometab', 'id', 'sometab.id should not be a pk' ),
    false,
    'col_isnt_pk( table, column, description )',
    'sometab.id should not be a pk',
    '        have: {id}
        want: anything else'
);

SELECT * FROM check_test(
    col_isnt_pk( 'sometab', 'id' ),
    false,
    'col_isnt_pk( table, column )',
    'Column sometab(id) should not be a primary key',
    '        have: {id}
        want: anything else'
);

SELECT * FROM check_test(
    col_isnt_pk( 'public', 'sometab', 'name', 'public.sometab.name should not be a pk' ),
    true,
    'col_isnt_pk( schema, table, column, description ) pass',
    'public.sometab.name should not be a pk',
    ''
);

SELECT * FROM check_test(
    col_isnt_pk( 'sometab', 'name', 'sometab.name should not be a pk' ),
    true,
    'col_isnt_pk( table, column, description ) pass',
    'sometab.name should not be a pk',
    ''
);

/****************************************************************************/
-- Test col_isnt_pk() with an array of columns.

SELECT * FROM check_test(
    col_isnt_pk( 'public', 'argh', ARRAY['id', 'foo'], 'id + foo should not be a pk' ),
    true,
    'col_isnt_pk( schema, table, column[], description )',
    'id + foo should not be a pk',
    ''
);

SELECT * FROM check_test(
    col_isnt_pk( 'argh', ARRAY['id', 'foo'], 'id + foo should not be a pk' ),
    true,
    'col_isnt_pk( table, column[], description )',
    'id + foo should not be a pk',
    ''
);

SELECT * FROM check_test(
    col_isnt_pk( 'argh', ARRAY['id', 'foo'] ),
    true,
    'col_isnt_pk( table, column[] )',
    'Columns argh(id, foo) should not be a primary key',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
