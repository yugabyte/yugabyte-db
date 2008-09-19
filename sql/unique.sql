\set ECHO
\i test_setup.sql

-- $Id$

SELECT plan(26);

-- This will be rolled back. :-)
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '' UNIQUE,
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);

/****************************************************************************/
-- Test has_unique().

\echo ok 1 - test has_unique( schema, table, description )
SELECT is(
    has_unique( 'public', 'sometab', 'public.sometab should have a unique constraint' ),
    'ok 1 - public.sometab should have a unique constraint',
    'has_unique( schema, table, description ) should work'
);

\echo ok 3 - test has_unique( table, description )
SELECT is(
    has_unique( 'sometab', 'sometab should have a unique constraint' ),
    'ok 3 - sometab should have a unique constraint',
    'has_unique( table, description ) should work'
);

\echo ok 5 - test has_unique( table )
SELECT is(
    has_unique( 'sometab' ),
    'ok 5 - Table sometab should have a unique constraint',
    'has_unique( table ) should work'
);

\echo ok 7 - test has_unique( schema, table, description ) fail
SELECT is(
    has_unique( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a unique constraint' ),
    'not ok 7 - pg_catalog.pg_class should have a unique constraint
# Failed test 7: "pg_catalog.pg_class should have a unique constraint"',
    'has_unique( schema, table, description ) should fail properly'
);

\echo ok 9 - test has_unique( table, description ) fail
SELECT is(
    has_unique( 'pg_class', 'pg_class should have a unique constraint' ),
    'not ok 9 - pg_class should have a unique constraint
# Failed test 9: "pg_class should have a unique constraint"',
    'has_unique( table, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 7, 9 );

/****************************************************************************/
-- Test col_is_unique().

\echo ok 11 - test col_is_unique( schema, table, column, description )
SELECT is(
    col_is_unique( 'public', 'sometab', 'name', 'public.sometab.name should be a pk' ),
    'ok 11 - public.sometab.name should be a pk',
    'col_is_unique( schema, table, column, description ) should work'
);

\echo ok 13 - test col_is_unique( table, column, description )
SELECT is(
    col_is_unique( 'sometab', 'name', 'sometab.name should be a pk' ),
    'ok 13 - sometab.name should be a pk',
    'col_is_unique( table, column, description ) should work'
);

\echo ok 15 - test col_is_unique( table, column )
SELECT is(
    col_is_unique( 'sometab', 'name' ),
    'ok 15 - Column sometab(name) should have a unique constraint',
    'col_is_unique( table, column ) should work'
);

\echo ok 17 - test col_is_unique( schema, table, column, description ) fail
SELECT is(
    col_is_unique( 'public', 'sometab', 'id', 'public.sometab.id should be a pk' ),
    'not ok 17 - public.sometab.id should be a pk
# Failed test 17: "public.sometab.id should be a pk"
#         have: {name}
#         want: {id}',
    'col_is_unique( schema, table, column, description ) should fail properly'
);

\echo ok 19 - test col_is_unique( table, column, description ) fail
SELECT is(
    col_is_unique( 'sometab', 'id', 'sometab.id should be a pk' ),
    'not ok 19 - sometab.id should be a pk
# Failed test 19: "sometab.id should be a pk"
#         have: {name}
#         want: {id}',
    'col_is_unique( table, column, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 17, 19 );

/****************************************************************************/
-- Test col_is_unique() with an array of columns.

CREATE TABLE public.argh (id int not null, name text not null, unique (id, name));

\echo ok 21 - test col_is_unique( schema, table, column[], description )
SELECT is(
    col_is_unique( 'public', 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    'ok 21 - id + name should be a pk',
    'col_is_unique( schema, table, column[], description ) should work'
);

\echo ok 23 - test col_is_unique( table, column[], description )
SELECT is(
    col_is_unique( 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    'ok 23 - id + name should be a pk',
    'col_is_unique( table, column[], description ) should work'
);

\echo ok 25 - test col_is_unique( table, column[], description )
SELECT is(
    col_is_unique( 'argh', ARRAY['id', 'name'] ),
    'ok 25 - Columns argh(id, name) should have a unique constraint',
    'col_is_unique( table, column[] ) should work'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
