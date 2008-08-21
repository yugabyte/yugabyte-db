\set ECHO

--
-- Tests for pgTAP.
--
--
-- $Id$

-- Format the output for nice TAP.
\pset format unaligned
\pset tuples_only true
\pset pager

-- Create plpgsql if it's not already there.
SET client_min_messages = fatal;
CREATE LANGUAGE plpgsql;

-- Keep things quiet.
SET client_min_messages = warning;

-- Revert all changes on failure.
\set ON_ERROR_ROLBACK 1
\set ON_ERROR_STOP true

-- Load the TAP functions.
BEGIN;
\i pgtap.sql

-- ## SET search_path TO TAPSCHEMA,public;

-- Set the test plan.
SELECT plan(26);

-- This will be rolled back. :-)
CREATE TABLE sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);

/****************************************************************************/
-- Test has_pk().

\echo ok 1 - test has_pk( schema, table, description )
SELECT is(
    has_pk( 'public', 'sometab', 'public.sometab should have a pk' ),
    'ok 1 - public.sometab should have a pk',
    'has_pk( schema, table, description ) should work'
);

\echo ok 3 - test has_pk( table, description )
SELECT is(
    has_pk( 'sometab', 'sometab should have a pk' ),
    'ok 3 - sometab should have a pk',
    'has_pk( table, description ) should work'
);

\echo ok 5 - test has_pk( table )
SELECT is(
    has_pk( 'sometab' ),
    'ok 5 - Table sometab should have a primary key',
    'has_pk( table ) should work'
);

\echo ok 7 - test has_pk( schema, table, description ) fail
SELECT is(
    has_pk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a pk' ),
    E'not ok 7 - pg_catalog.pg_class should have a pk\n# Failed test 7: "pg_catalog.pg_class should have a pk"',
    'has_pk( schema, table, description ) should fail properly'
);

\echo ok 9 - test has_pk( table, description ) fail
SELECT is(
    has_pk( 'pg_class', 'pg_class should have a pk' ),
    E'not ok 9 - pg_class should have a pk\n# Failed test 9: "pg_class should have a pk"',
    'has_pk( table, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 7, 9 );

/****************************************************************************/
-- Test col_is_pk().

\echo ok 11 - test col_is_pk( schema, table, column, description )
SELECT is(
    col_is_pk( 'public', 'sometab', 'id', 'public.sometab.id should be a pk' ),
    'ok 11 - public.sometab.id should be a pk',
    'col_is_pk( schema, table, column, description ) should work'
);

\echo ok 13 - test col_is_pk( table, column, description )
SELECT is(
    col_is_pk( 'sometab', 'id', 'sometab.id should be a pk' ),
    'ok 13 - sometab.id should be a pk',
    'col_is_pk( table, column, description ) should work'
);

\echo ok 15 - test col_is_pk( table, column )
SELECT is(
    col_is_pk( 'sometab', 'id' ),
    'ok 15 - Column sometab.id should be a primary key',
    'col_is_pk( table, column ) should work'
);

\echo ok 17 - test col_is_pk( schema, table, column, description ) fail
SELECT is(
    col_is_pk( 'public', 'sometab', 'name', 'public.sometab.name should be a pk' ),
    E'not ok 17 - public.sometab.name should be a pk\n# Failed test 17: "public.sometab.name should be a pk"\n#         have: {id}\n#         want: {name}',
    'col_is_pk( schema, table, column, description ) should fail properly'
);

\echo ok 19 - test col_is_pk( table, column, description ) fail
SELECT is(
    col_is_pk( 'sometab', 'name', 'sometab.name should be a pk' ),
    E'not ok 19 - sometab.name should be a pk\n# Failed test 19: "sometab.name should be a pk"\n#         have: {id}\n#         want: {name}',
    'col_is_pk( table, column, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 17, 19 );

/****************************************************************************/
-- Test col_is_pk() with an array of columns.

CREATE TABLE argh (id int not null, name text not null, primary key (id, name));

\echo ok 21 - test col_is_pk( schema, table, column[], description )
SELECT is(
    col_is_pk( 'public', 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    'ok 21 - id + name should be a pk',
    'col_is_pk( schema, table, column[], description ) should work'
);

\echo ok 23 - test col_is_pk( table, column[], description )
SELECT is(
    col_is_pk( 'argh', ARRAY['id', 'name'], 'id + name should be a pk' ),
    'ok 23 - id + name should be a pk',
    'col_is_pk( table, column[], description ) should work'
);

\echo ok 25 - test col_is_pk( table, column[], description )
SELECT is(
    col_is_pk( 'argh', ARRAY['id', 'name'] ),
    'ok 25 - Column argh.{id,name} should be a primary key',
    'col_is_pk( table, column[] ) should work'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
