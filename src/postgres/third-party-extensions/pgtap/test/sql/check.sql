\unset ECHO
\i test/setup.sql

SELECT plan(48);

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '' CHECK ( name IN ('foo', 'bar', 'baz') ),
    numb  NUMERIC(10, 2),
    myint NUMERIC(8),
    CHECK (numb > 1.0 AND myint < 10)
);
RESET client_min_messages;

/****************************************************************************/
-- Test has_check().

SELECT * FROM check_test(
    has_check( 'public', 'sometab', 'public.sometab should have a check constraint' ),
    true,
    'has_check( schema, table, desc )',
    'public.sometab should have a check constraint',
    ''
);

SELECT * FROM check_test(
    has_check( 'sometab', 'sometab should have a check constraint' ),
    true,
    'has_check( table, desc )',
    'sometab should have a check constraint',
    ''
);

SELECT * FROM check_test(
    has_check( 'sometab' ),
    true,
    'has_check( table )',
    'Table sometab should have a check constraint',
    ''
);

SELECT * FROM check_test(
    has_check( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a check constraint' ),
    false,
    'has_check( schema, table, descr ) fail',
    'pg_catalog.pg_class should have a check constraint',
    ''
);

SELECT * FROM check_test(
    has_check( 'pg_class', 'pg_class should have a check constraint' ),
    false,
    'has_check( table, desc ) fail',
    'pg_class should have a check constraint',
    ''
);

/****************************************************************************/
-- Test col_has_check().

SELECT * FROM check_test(
    col_has_check( 'public', 'sometab', 'name', 'public.sometab.name should have a check' ),
    true,
    'col_has_check( sch, tab, col, desc )',
    'public.sometab.name should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'public', 'sometab', ARRAY['numb', 'myint'], 'public.sometab.numb+myint should have a check' ),
    true,
    'col_has_check( sch, tab, cols, desc )',
    'public.sometab.numb+myint should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'sometab', 'name', 'sometab.name should have a check' ),
    true,
    'col_has_check( tab, col, desc )',
    'sometab.name should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'sometab', ARRAY['numb', 'myint'], 'sometab.numb+myint should have a check' ),
    true,
    'col_has_check( tab, cols, desc )',
    'sometab.numb+myint should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'sometab', 'name' ),
    true,
    'col_has_check( table, column )',
    'Column sometab(name) should have a check constraint',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'sometab', ARRAY['numb', 'myint'] ),
    true,
    'col_has_check( table, columns )',
    'Columns sometab(numb, myint) should have a check constraint',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'public', 'sometab', 'id', 'public.sometab.id should have a check' ),
    false,
    'col_has_check( sch, tab, col, desc ) fail',
    'public.sometab.id should have a check',
    '        have: {name}
              {numb,myint}
        want: {id}'
);

SELECT * FROM check_test(
    col_has_check( 'sometab', 'id', 'sometab.id should have a check' ),
    false,
    'col_has_check( tab, col, desc ) fail',
    'sometab.id should have a check',
    '        have: {name}
              {numb,myint}
        want: {id}'
);

/****************************************************************************/
-- Test col_has_check() with an array of columns.

SET LOCAL client_min_messages = warning;
CREATE TABLE public.argh (
    id   INT  NOT NULL,
    name TEXT NOT NULL,
    CHECK ( id IN (1, 2) AND name IN ('foo', 'bar'))
);
RESET client_min_messages;

SELECT * FROM check_test(
    col_has_check( 'public', 'argh', ARRAY['id', 'name'], 'id + name should have a check' ),
    true,
    'col_has_check( sch, tab, col[], desc )',
    'id + name should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'argh', ARRAY['id', 'name'], 'id + name should have a check' ),
    true,
    'col_has_check( tab, col[], desc )',
    'id + name should have a check',
    ''
);

SELECT * FROM check_test(
    col_has_check( 'argh', ARRAY['id', 'name'] ),
    true,
    'col_has_check( tab, col[] )',
    'Columns argh(id, name) should have a check constraint',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
