\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(39);

-- This will be rolled back. :-)
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);

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
    has_pk( 'sometab', 'sometab should have a pk' ),
    true,
    'has_pk( table, description )',
    'sometab should have a pk',
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
    has_pk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a pk' ),
    false,
    'has_pk( schema, table, description ) fail',
    'pg_catalog.pg_class should have a pk',
    ''
);

SELECT * FROM check_test(
    has_pk( 'pg_class', 'pg_class should have a pk' ),
    false,
    'has_pk( table, description ) fail',
    'pg_class should have a pk',
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
    '       have: {id}
        want: {name}'
);

SELECT * FROM check_test(
    col_is_pk( 'sometab', 'name', 'sometab.name should be a pk' ),
    false,
    'col_is_pk( table, column, description ) fail',
    'sometab.name should be a pk',
    '       have: {id}
        want: {name}'
);

/****************************************************************************/
-- Test col_is_pk() with an array of columns.

CREATE TABLE public.argh (
    id INT NOT NULL,
    name TEXT NOT NULL,
    PRIMARY KEY (id, name)
);

SELECT * FROM check_test(
    col_is_pk( 'public', 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    true,
    'col_is_pk( schema, table, column[], description )',
    'id + name should be a pk',
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
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
