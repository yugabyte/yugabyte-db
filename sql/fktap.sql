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
SELECT plan(64);

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
SELECT * FROM check_test(
    has_fk( 'public', 'fk', 'public.fk should have an fk' ),
    true,
    'has_fk( schema, table, description )',
    'public.fk should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'fk', 'fk should have a pk' ),
    'true',
    'has_fk( table, description )',
    'fk should have a pk'
);

SELECT * FROM check_test(
    has_fk( 'fk' ),
    true,
    'has_fk( table )',
    'Table fk should have a foreign key constraint'
);

SELECT * FROM check_test(
    has_fk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have a pk' ),
    false,
    'has_fk( schema, table, description ) fail',
    'pg_catalog.pg_class should have a pk'
);

SELECT * FROM check_test(
    has_fk( 'pg_class', 'pg_class should have a pk' ),
    false,
    'has_fk( table, description ) fail',
    'pg_class should have a pk'
);

/****************************************************************************/
-- Test col_is_fk().

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk', 'pk_id', 'public.fk.pk_id should be an fk' ),
    true,
    'col_is_fk( schema, table, column, description )',
    'public.fk.pk_id should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk', 'pk_id', 'fk.pk_id should be an fk' ),
    true,
    'col_is_fk( table, column, description )',
    'fk.pk_id should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk', 'pk_id' ),
    true,
    'col_is_fk( table, column )',
    'Column fk.pk_id should be a foreign key'
);

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk', 'name', 'public.fk.name should be an fk' ),
    false,
    'col_is_fk( schema, table, column, description )',
    'public.fk.name should be an fk',
    E'        have: {pk_id}\n        want: {name}'
);

SELECT * FROM check_test(
    col_is_fk( 'fk', 'name', 'fk.name should be an fk' ),
    false,
    'col_is_fk( table, column, description )',
    'fk.name should be an fk',
    E'        have: {pk_id}\n        want: {name}'
);

/****************************************************************************/
-- Test col_is_fk() with an array of columns.

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    true,
    'col_is_fk( schema, table, column[], description )',
    'id + pk2_dot should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    true,
    'col_is_fk( table, column[], description )',
    'id + pk2_dot should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'] ),
    true,
    'col_is_fk( table, column[] )',
    'Columns fk2.{pk2_num,pk2_dot} should be a foreign key'
);

/****************************************************************************/
-- Test fk_ok().
SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'full fk_ok array',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'public', 'pk2', ARRAY['num', 'dot'] ),
    true,
    'multiple fk fk_ok desc',
    'public.fk2(pk2_num, pk2_dot) should reference public.pk2(num, dot)'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'] ),
    true,
    'fk_ok array desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'] ),
    true,
    'fk_ok array noschema desc',
    'fk(pk_id) should reference pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'pk2', ARRAY['num', 'dot'] ),
    true,
    'multiple fk fk_ok noschema desc',
    'fk2(pk2_num, pk2_dot) should reference pk2(num, dot)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'fk_ok array noschema',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id' ),
    true,
    'basic fk_ok desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok noschema',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id' ),
    true,
    'basic fk_ok noschema desc',
    'fk(pk_id) should reference pk(id)'
);

-- Make sure check_test() works properly with no name argument.
SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id' ),
    true
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok fail',
    'WHATEVER',
    '        have: public.fk(pk_id) REFERENCES public.pk(id)
        want: public.fk(pk_id) REFERENCES public.pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['fid'] ),
    false,
    'fk_ok fail desc',
    'public.fk(pk_id) should reference public.pk(fid)',
    '        have: public.fk(pk_id) REFERENCES public.pk(id)
        want: public.fk(pk_id) REFERENCES public.pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok fail no schema',
    'WHATEVER',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['fid'] ),
    false,
    'fk_ok fail no schema desc',
    'fk(pk_id) should reference pk(fid)',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'ok', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok bad PK test',
    'WHATEVER',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES ok(fid)'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
