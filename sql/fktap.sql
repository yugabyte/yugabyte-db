\set ECHO
\set QUIET 1

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
\set ON_ERROR_STOP off
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
SELECT plan(45);

-- These will be rolled back. :-)
CREATE TABLE pk (
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
);

CREATE TABLE fk (
    id    INT NOT NULL PRIMARY KEY,
    pk_id INT NOT NULL REFERENCES pk(id)
);

CREATE TABLE pk2 (
    num int NOT NULL,
    dot int NOT NULL,
    PRIMARY KEY (num, dot)
);

CREATE TABLE fk2 (
    pk2_num int NOT NULL,
    pk2_dot int NOT NULL,
    FOREIGN KEY(pk2_num, pk2_dot) REFERENCES pk2( num, dot)
);

/****************************************************************************/
-- Test has_fk().

\echo ok 1 - test has_fk( schema, table, description )
SELECT is(
    has_fk( 'public', 'fk', 'public.fk should have an fk' ),
    'ok 1 - public.fk should have an fk',
    'has_fk( schema, table, description ) should work'
);

\echo ok 3 - test has_fk( table, description )
SELECT is(
    has_fk( 'fk', 'fk should have a pk' ),
    'ok 3 - fk should have a pk',
    'has_fk( table, description ) should work'
);

\echo ok 5 - test has_fk( table )
SELECT is(
    has_fk( 'fk' ),
    'ok 5 - Table fk should have a foreign key constraint',
    'has_fk( table ) should work'
);

\echo ok 7 - test has_fk( schema, table, description ) fail
SELECT is(
    has_fk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a pk' ),
    E'not ok 7 - pg_catalog.pg_class should have a pk\n# Failed test 7: "pg_catalog.pg_class should have a pk"',
    'has_fk( schema, table, description ) should fail properly'
);

\echo ok 9 - test has_fk( table, description ) fail
SELECT is(
    has_fk( 'pg_class', 'pg_class should have a pk' ),
    E'not ok 9 - pg_class should have a pk\n# Failed test 9: "pg_class should have a pk"',
    'has_fk( table, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 7, 9 );

/****************************************************************************/
-- Test col_is_fk().

\echo ok 11 - test col_is_fk( schema, table, column, description )
SELECT is(
    col_is_fk( 'public', 'fk', 'pk_id', 'public.fk.pk_id should be an fk' ),
    'ok 11 - public.fk.pk_id should be an fk',
    'col_is_fk( schema, table, column, description ) should work'
);

\echo ok 13 - test col_is_fk( table, column, description )
SELECT is(
    col_is_fk( 'fk', 'pk_id', 'fk.pk_id should be an fk' ),
    'ok 13 - fk.pk_id should be an fk',
    'col_is_fk( table, column, description ) should work'
);

\echo ok 15 - test col_is_fk( table, column )
SELECT is(
    col_is_fk( 'fk', 'pk_id' ),
    'ok 15 - Column fk.pk_id should be a foreign key',
    'col_is_fk( table, column ) should work'
);

\echo ok 17 - test col_is_fk( schema, table, column, description ) fail
SELECT is(
    col_is_fk( 'public', 'fk', 'name', 'public.fk.name should be an fk' ),
    E'not ok 17 - public.fk.name should be an fk\n# Failed test 17: "public.fk.name should be an fk"\n#         have: {pk_id}\n#         want: {name}',
    'col_is_fk( schema, table, column, description ) should fail properly'
);

\echo ok 19 - test col_is_fk( table, column, description ) fail
SELECT is(
    col_is_fk( 'fk', 'name', 'fk.name should be an fk' ),
    E'not ok 19 - fk.name should be an fk\n# Failed test 19: "fk.name should be an fk"\n#         have: {pk_id}\n#         want: {name}',
    'col_is_fk( table, column, description ) should fail properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 17, 19 );

/****************************************************************************/
-- Test col_is_fk() with an array of columns.

\echo ok 21 - test col_is_fk( schema, table, column[], description )
SELECT is(
    col_is_fk( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    'ok 21 - id + pk2_dot should be an fk',
    'col_is_fk( schema, table, column[], description ) should work'
);

\echo ok 23 - test col_is_fk( table, column[], description )
SELECT is(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    'ok 23 - id + pk2_dot should be an fk',
    'col_is_fk( table, column[], description ) should work'
);

\echo ok 25 - test col_is_fk( table, column[], description )
SELECT is(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'] ),
    'ok 25 - Columns fk2.{pk2_num,pk2_dot} should be a foreign key',
    'col_is_fk( table, column[] ) should work'
);

/****************************************************************************/
-- Test fk_ok().
SELECT * FROM test_ok(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'full fk_ok array',
    'WHATEVER'
);

SELECT * FROM test_ok(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'] ),
    true,
    'fk_ok array desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM test_ok(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'] ),
    true,
    'fk_ok array noschema desc',
    'fk(pk_id) should reference pk(id)'
);

SELECT * FROM test_ok(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'fk_ok array noschema',
    'WHATEVER'
);

SELECT * FROM test_ok(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok',
    'WHATEVER'
);

SELECT * FROM test_ok(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id' ),
    true,
    'basic fk_ok desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM test_ok(
    fk_ok( 'fk', 'pk_id', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok noschema',
    'WHATEVER'
);

SELECT * FROM test_ok(
    fk_ok( 'fk', 'pk_id', 'pk', 'id' ),
    true,
    'basic fk_ok noschema desc',
    'fk(pk_id) should reference pk(id)'
);

SELECT * FROM test_ok(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok fail',
    'WHATEVER',
    '        have: public.fk(pk_id) REFERENCES public.pk(id)
        want: public.fk(pk_id) REFERENCES public.pk(fid)'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
