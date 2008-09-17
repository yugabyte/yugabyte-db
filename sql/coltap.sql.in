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
SELECT plan(38);

-- This will be rolled back. :-)
CREATE TABLE sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);

/****************************************************************************/
-- Test col_not_null().
\echo ok 1 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'pg_catalog', 'pg_type', 'typname', 'typname not null' ),
    'ok 1 - typname not null',
    'col_not_null( schema, table, column, desc ) should work'
);
\echo ok 3 - testing col_not_null( table, column, desc )
SELECT is(
    col_not_null( 'sometab', 'id', 'blah blah blah' ),
    'ok 3 - blah blah blah',
    'col_not_null( table, column, desc ) should work'
);

\echo ok 5 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'sometab', 'id' ),
    'ok 5 - Column sometab(id) should be NOT NULL',
    'col_not_null( table, column ) should work'
);
-- Make sure failure is correct.
\echo ok 7 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'sometab', 'name' ),
    E'not ok 7 - Column sometab(name) should be NOT NULL\n# Failed test 7: "Column sometab(name) should be NOT NULL"',
    'col_not_null( table, column ) should properly fail'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 7 );

/****************************************************************************/
-- Test col_is_null().
\echo ok 9 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'public', 'sometab', 'name', 'name is null' ),
    'ok 9 - name is null',
    'col_is_null( schema, table, column, desc ) should work'
);
\echo ok 11 - testing col_is_null( table, column, desc )
SELECT is(
    col_is_null( 'sometab', 'name', 'my desc' ),
    'ok 11 - my desc',
    'col_is_null( table, column, desc ) should work'
);

\echo ok 13 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'sometab', 'name' ),
    'ok 13 - Column sometab(name) should allow NULL',
    'col_is_null( table, column ) should work'
);
-- Make sure failure is correct.
\echo ok 15 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'sometab', 'id' ),
    E'not ok 15 - Column sometab(id) should allow NULL\n# Failed test 15: "Column sometab(id) should allow NULL"',
    'col_is_null( table, column ) should properly fail'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 15 );

/****************************************************************************/
-- Test col_type_is().
\echo ok 17 - testing col_type_is( schema, table, column, type, desc )
SELECT is(
    col_type_is( 'public', 'sometab', 'name', 'text', 'name is text' ),
    'ok 17 - name is text',
    'col_type_is( schema, table, column, type, desc ) should work'
);

\echo ok 19 - testing col_type_is( table, column, type, desc )
SELECT is(
    col_type_is( 'sometab', 'name', 'text', 'yadda yadda yadda' ),
    'ok 19 - yadda yadda yadda',
    'col_type_is( table, column, type, desc ) should work'
);

\echo ok 21 - testing col_type_is( table, column, type )
SELECT is(
    col_type_is( 'sometab', 'name', 'text' ),
    'ok 21 - Column sometab(name) should be type text',
    'col_type_is( table, column, type ) should work'
);

\echo ok 23 - testing col_type_is( table, column, type ) case-insensitively
SELECT is(
    col_type_is( 'sometab', 'name', 'TEXT' ),
    'ok 23 - Column sometab(name) should be type TEXT',
    'col_type_is( table, column, type ) should work case-insensitively'
);

-- Make sure failure is correct.
\echo ok 25 - testing col_type_is( table, column, type ) failure
SELECT is(
    col_type_is( 'sometab', 'name', 'int4' ),
    E'not ok 25 - Column sometab(name) should be type int4\n# Failed test 25: "Column sometab(name) should be type int4"\n#         have: text\n#         want: int4',
    'col_type_is( table, column, type ) should fail with proper diagnostics'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 25 );

/****************************************************************************/
-- Try col_type_is() with precision.
\echo ok 27 - testing col_type_is( schema, table, column, type(precision,scale), description )
SELECT is(
    col_type_is( 'public', 'sometab', 'numb', 'numeric(10,2)', 'lol' ),
    'ok 27 - lol',
    'col_type_is( schema, table, column, type, precision(scale,description) should work'
);

-- Check its diagnostics.
\echo ok 29 - col_type_is( table, column, type, precision, desc ) fail
SELECT is(
    col_type_is( 'sometab', 'myint', 'numeric(7)', 'should be numeric(7)' ),
    E'not ok 29 - should be numeric(7)\n# Failed test 29: "should be numeric(7)"\n#         have: numeric(8,0)\n#         want: numeric(7)',
    'col_type_is with precision should have nice diagnostics'
);

UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 29 );

/****************************************************************************/
-- Test col_default_is().

\echo ok 31 - col_default_is( schema, table, column, default, description )
SELECT is(
    col_default_is( 'public', 'sometab', 'name', ''::text, 'name should default to empty string' ),
    'ok 31 - name should default to empty string',
    'col_default_is( schema, table, column, default, description ) should work'
);

\echo ok 33 - col_default_is( schema, table, column, default, description ) fail
SELECT is(
    col_default_is( 'public', 'sometab', 'name', 'foo'::text, 'name should default to ''foo''' ),
    E'not ok 33 - name should default to ''foo''\n# Failed test 33: "name should default to ''foo''"\n#         have: \n#         want: foo',
    'ok 33 - Should get proper diagnostics for a default failure'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 33 );

\echo ok 35 - col_default_is( table, column, default, description )
SELECT is(
    col_default_is( 'sometab', 'name', ''::text, 'name should default to empty string' ),
    'ok 35 - name should default to empty string',
    'col_default_is( table, column, default, description ) should work'
);

\echo ok 37 - col_default_is( table, column, default )
SELECT is(
    col_default_is( 'sometab', 'name', '' ),
    'ok 37 - Column sometab(name) should default to ''''',
    'col_default_is( table, column, default ) should work'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
