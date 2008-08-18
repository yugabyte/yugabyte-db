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
\set numb_tests 143

-- ## SET search_path TO TAPSCHEMA,public;

-- Set the test plan.
SELECT plan(:numb_tests);

-- Replace the internal record of the plan for a few tests.
UPDATE  __tcache__ SET value = 3 WHERE label = 'plan';

/****************************************************************************/
-- Test pass().
SELECT pass( 'My pass() passed, w00t!' );

-- Test fail().
\set fail_numb 2
\echo ok :fail_numb - Testing fail()
SELECT is(
       fail('oops'),
       E'not ok 2 - oops\n# Failed test 2: "oops"', 'We should get the proper output from fail()');

-- Check the finish() output.
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you failed 1 test of 3',
    'The output of finish() should reflect the test failure'
);

/****************************************************************************/
-- Check num_failed
SELECT is( num_failed(), 1, 'We should have one failure' );
UPDATE __tresults__ SET ok = true, aok = true WHERE numb = :fail_numb;
SELECT is( num_failed(), 0, 'We should now have no failures' );

/****************************************************************************/
-- Check diag.
SELECT is( diag('foo'), '# foo', 'diag() should work properly' );
SELECT is( diag(E'foo\nbar'), E'# foo\n# bar', 'multiline diag() should work properly' );
SELECT is( diag(E'foo\n# bar'), E'# foo\n# # bar', 'multiline diag() should work properly with existing comments' );

/****************************************************************************/
-- Check no_plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT * FROM no_plan();
SELECT is( value, 0, 'no_plan() should have stored a plan of 0' )
  FROM __tcache__
 WHERE label = 'plan';

-- Set the plan to a high number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4000), '1..4000', 'Set the plan to 4000' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4000 tests but only ran 11',
    'The output of finish() should reflect a high test plan'
);

-- Set the plan to a low number.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(4), '1..4', 'Set the plan to 4' );
SELECT is(
    (SELECT * FROM finish() LIMIT 1),
    '# Looks like you planned 4 tests but ran 9 extra',
    'The output of finish() should reflect a low test plan'
);

-- Reset the original plan.
DELETE FROM __tcache__ WHERE label = 'plan';
SELECT is( plan(:numb_tests), '1..' || :numb_tests, 'Reset the plan' );
SELECT is( value, :numb_tests, 'plan() should have stored the test count' )
  FROM __tcache__
 WHERE label = 'plan';

/****************************************************************************/
-- Test ok()
\echo ok 17 - ok() success
SELECT is( ok(true), 'ok 17', 'ok(true) should work' );
\echo ok 19 - ok() success 2
SELECT is( ok(true, ''), 'ok 19', 'ok(true, '''') should work' );
\echo ok 21 - ok() success 3
SELECT is( ok(true, 'foo'), 'ok 21 - foo', 'ok(true, ''foo'') should work' );

\echo ok 23 - ok() failure
SELECT is( ok(false), E'not ok 23\n# Failed test 23', 'ok(false) should work' );
\echo ok 25 - ok() failure 2
SELECT is( ok(false, ''), E'not ok 25\n# Failed test 25', 'ok(false, '''') should work' );
\echo ok 27 - ok() failure 3
SELECT is( ok(false, 'foo'), E'not ok 27 - foo\n# Failed test 27: "foo"', 'ok(false, ''foo'') should work' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 23, 25, 27);

/****************************************************************************/
-- Test is().
\echo ok 29 - is() success
SELECT is( is(1, 1), 'ok 29', 'isa(1, 1) should work' );
\echo ok 31 - is() success 2
SELECT is( is('x'::text, 'x'::text), 'ok 31', 'is(''x'', ''x'') should work' );
\echo ok 33 - is() success 3
SELECT is( is(1.1, 1.10), 'ok 33', 'is(1.1, 1.10) should work' );
\echo ok 35 - is() success 4
SELECT is( is(1.1, 1.10), 'ok 35', 'is(1.1, 1.10) should work' );
\echo ok 37 - is() success 5
SELECT is( is(true, true), 'ok 37', 'is(true, true) should work' );
\echo ok 39 - is() success 6
SELECT is( is(false, false), 'ok 39', 'is(false, false) should work' );
--SELECT is( '12:45'::time, '12:45'::time, 'ok 41', 'is(time, time) should work' );
\echo ok 41 - is() success 7
SELECT is( is(1, 1, 'foo'), 'ok 41 - foo', 'is(1, 1, ''foo'') should work' );
\echo ok 43 - is() failure
SELECT is( is( 1, 2 ), E'not ok 43\n# Failed test 43\n#         have: 1\n#         want: 2', 'is(1, 2) should work' );

/****************************************************************************/
-- Test isnt().
\echo ok 45 - isnt() success
SELECT is( isnt(1, 2), 'ok 45', 'isnt(1, 2) should work' );
\echo ok 47 - isnt() failure
SELECT is( isnt( 1, 1 ), E'not ok 47\n# Failed test 47\n#     1\n#       <>\n#     1', 'is(1, 2) should work' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 43, 47 );

/****************************************************************************/
-- Try using variables.
\set foo '\'' waffle '\''
\set bar '\'' waffle '\''
SELECT is( :foo::text, :bar::text, 'is() should work with psql variables' );

/****************************************************************************/
-- Test matches().
SELECT matches( 'foo'::text, 'o', 'matches() should work' );
SELECT matches( 'foo'::text, '^fo', 'matches() should work with a regex' );
SELECT imatches( 'FOO'::text, '^fo', 'imatches() should work with a regex' );

-- Check matches() diagnostics.
\echo ok 53 - matches() failure
SELECT is( matches( 'foo'::text, '^a' ), E'not ok 53\n# Failed test 53\n#                   ''foo''\n#    doesn''t match: ''^a''', 'Check matches diagnostics' );

-- Check doesnt_match.
SELECT doesnt_match( 'foo'::text, 'a', 'doesnt_match() should work' );
SELECT doesnt_match( 'foo'::text, '^o', 'doesnt_match() should work with a regex' );
SELECT doesnt_imatch( 'foo'::text, '^o', 'doesnt_imatch() should work with a regex' );

-- Check doesnt_match diagnostics.
\echo ok 58 - doesnt_match() failure
SELECT is(
    doesnt_match( 'foo'::text, 'o' ),
    E'not ok 58\n# Failed test 58\n#                   ''foo''\n#          matches: ''o''',
    'doesnt_match() should work'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 53, 58 );

/****************************************************************************/
-- Test alike().
SELECT alike( 'foo'::text, 'foo', 'alike() should work' );
SELECT alike( 'foo'::text, 'fo%', 'alike() should work with a regex' );
SELECT ialike( 'FOO'::text, 'fo%', 'ialike() should work with a regex' );

-- Check alike() diagnostics.
\echo ok 63 - alike() failure
SELECT is( alike( 'foo'::text, 'a%'::text ), E'not ok 63\n# Failed test 63\n#                   ''foo''\n#    doesn''t match: ''a%''', 'Check alike diagnostics' );

-- Test unalike().
SELECT unalike( 'foo'::text, 'f', 'unalike() should work' );
SELECT unalike( 'foo'::text, 'f%i', 'unalike() should work with a regex' );
SELECT unialike( 'FOO'::text, 'f%i', 'iunalike() should work with a regex' );

-- Check unalike() diagnostics.
\echo ok 68 - unalike() failure
SELECT is( unalike( 'foo'::text, 'f%'::text ), E'not ok 68\n# Failed test 68\n#                   ''foo''\n#          matches: ''f%''', 'Check unalike diagnostics' );

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 63, 68 );

/****************************************************************************/
-- test throws_ok().
SELECT throws_ok( 'SELECT 1 / 0', '22012', 'throws_ok(1/0) should work' );

-- Check its diagnostics for an invalid error code.
\echo ok 71 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1 / 0', 97212 ),
    E'not ok 71 - threw 97212\n# Failed test 71: "threw 97212"\n#       caught: 22012: division by zero\n#       wanted: 97212',
    'We should get the proper diagnostics from throws_ok()'
);

SELECT throws_ok( 'SELECT 1 / 0', NULL, 'throws_ok(1/0, NULL) should work' );

-- Check its diagnostics no error.
\echo ok 74 - throws_ok failure diagnostics
SELECT is(
    throws_ok( 'SELECT 1', NULL ),
    E'not ok 74 - threw an exception\n# Failed test 74: "threw an exception"\n#       caught: no exception\n#       wanted: an exception',
    'We should get the proper diagnostics from throws_ok() with a NULL error code'
);

-- Clean up the failed test results.
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 71, 74 );

/****************************************************************************/
-- test lives_ok().
SELECT lives_ok( 'SELECT 1', 'lives_ok() should work' );

-- Check its diagnostics when there is an exception.
\echo ok 77 - lives_ok failure diagnostics
SELECT is(
    lives_ok( 'SELECT 1 / 0' ),
    E'not ok 77\n# Failed test 77\n#         died: 22012: division by zero',
    'We should get the proper diagnostics for a lives_ok() failure'
);

UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 77 );
\echo ok 79 - lives_ok is ok

/****************************************************************************/
-- test multiline description.
SELECT is(
    ok( true, E'foo\nbar' ),
    E'ok 79 - foo\n# bar',
    'multiline desriptions should have subsequent lines escaped'
);

/****************************************************************************/
-- Test todo tests.
\echo ok 81 - todo fail
\echo ok 82 - todo pass
SELECT * FROM todo('just because', 2 );
SELECT is(
    fail('This is a todo test' )
    || pass('This is a todo test that unexpectedly passes' ),
    'not ok 81 - This is a todo test # TODO just because
# Failed (TODO) test 81: "This is a todo test"ok 82 - This is a todo test that unexpectedly passes # TODO just because',
   'TODO tests should display properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 81 );


/****************************************************************************/
-- Test has_table().

\echo ok 84 - has_table(table) fail

SELECT is(
    has_table( '__SDFSDFD__' ),
    E'not ok 84 - Table public.__SDFSDFD__ should exist\n# Failed test 84: "Table public.__SDFSDFD__ should exist"',
    'has_table(table) should fail for non-existent table'
);

\echo ok 86 - has_table(schema, table) fail
SELECT is(
    has_table( 'foo', '__SDFSDFD__' ),
    E'not ok 86 - Table foo.__SDFSDFD__ should exist\n# Failed test 86: "Table foo.__SDFSDFD__ should exist"',
    'has_table(schema, table) should fail for non-existent table'
);

\echo ok 88 - has_table(schema, table, desc) fail
SELECT is(
    has_table( 'foo', '__SDFSDFD__', 'desc' ),
    E'not ok 88 - desc\n# Failed test 88: "desc"',
    'has_table(schema, table, desc) should fail for non-existent table'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 84, 86, 88 );

\echo ok 90 - has_table(schema, table) pass
SELECT is(
    has_table( 'pg_catalog', 'pg_type' ),
    'ok 90 - Table pg_catalog.pg_type should exist',
    'has_table(schema, table) should pass for an existing table'
);

\echo ok 92 - has_table(schema, table, desc) pass
SELECT is(
    has_table( 'pg_catalog', 'pg_type', 'desc' ),
    'ok 92 - desc',
    'has_table(schema, table, desc) should pass for an existing table'
);

/****************************************************************************/
-- Test has_view().

\echo ok 94 - has_view(view) fail

SELECT is(
    has_view( '__SDFSDFD__' ),
    E'not ok 94 - View public.__SDFSDFD__ should exist\n# Failed test 94: "View public.__SDFSDFD__ should exist"',
    'has_view(view) should fail for non-existent view'
);

\echo ok 96 - has_view(schema, view) fail
SELECT is(
    has_view( 'foo', '__SDFSDFD__' ),
    E'not ok 96 - View foo.__SDFSDFD__ should exist\n# Failed test 96: "View foo.__SDFSDFD__ should exist"',
    'has_view(schema, view) should fail for non-existent view'
);

\echo ok 98 - has_view(schema, view, desc) fail
SELECT is(
    has_view( 'foo', '__SDFSDFD__', 'desc' ),
    E'not ok 98 - desc\n# Failed test 98: "desc"',
    'has_view(schema, view, desc) should fail for non-existent view'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 94, 96, 98 );

\echo ok 100 - has_view(schema, view) pass
SELECT is(
    has_view( 'information_schema', 'tables' ),
    'ok 100 - View information_schema.tables should exist',
    'has_view(schema, view) should pass for an existing view'
);

\echo ok 102 - has_view(schema, view, desc) pass
SELECT is(
    has_view( 'information_schema', 'tables', 'desc' ),
    'ok 102 - desc',
    'has_view(schema, view, desc) should pass for an existing view'
);

/****************************************************************************/
-- Test has_column().

\echo ok 104 - has_column(table, column) fail
SELECT is(
    has_column( '__SDFSDFD__', 'foo' ),
    E'not ok 104 - Column public.__SDFSDFD__.foo should exist\n# Failed test 104: "Column public.__SDFSDFD__.foo should exist"',
    'has_column(table, column) should fail for non-existent table'
);

\echo ok 106 - has_column(schema, table, column) fail
SELECT is(
    has_column( 'foo', '__SDFSDFD__', 'bar' ),
    E'not ok 106 - Column foo.__SDFSDFD__.bar should exist\n# Failed test 106: "Column foo.__SDFSDFD__.bar should exist"',
    'has_column(schema, table, column) should fail for non-existent table'
);

\echo ok 108 - has_column(schema, table, column, desc) fail
SELECT is(
    has_column( 'foo', '__SDFSDFD__', 'bar', 'desc' ),
    E'not ok 108 - desc\n# Failed test 108: "desc"',
    'has_column(schema, table, column, desc) should fail for non-existent table'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 104, 106, 108 );

-- This will be rolled back. :-)
CREATE TABLE sometab (id int NOT NULL, name text, numb numeric(10, 2), myint numeric(8));

\echo ok 110 - has_column(table, column) pass
SELECT is(
    has_column( 'sometab', 'id' ),
    'ok 110 - Column public.sometab.id should exist',
    'has_column(table, column) should pass for an existing column'
);

\echo ok 112 - has_column(schema, column, desc) pass
SELECT is(
    has_column( 'information_schema', 'tables', 'table_name', 'desc' ),
    'ok 112 - desc',
    'has_column(schema, table, column, desc) should pass for an existing view column'
);

/****************************************************************************/
-- Test col_not_null().
\echo ok 114 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'pg_catalog', 'pg_type', 'typname', 'typname not null' ),
    'ok 114 - typname not null',
    'col_not_null( schema, table, column, desc ) should work'
);
\echo ok 116 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'pg_catalog', 'pg_type', 'typname' ),
    'ok 116 - Column pg_catalog.pg_type.typname should be NOT NULL',
    'col_not_null( schema, table, column ) should work'
);

\echo ok 118 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'sometab', 'id' ),
    'ok 118 - Column public.sometab.id should be NOT NULL',
    'col_not_null( table, column ) should work'
);
-- Make sure failure is correct.
\echo ok 120 - testing col_not_null( schema, table, column, desc )
SELECT is(
    col_not_null( 'sometab', 'name' ),
    E'not ok 120 - Column public.sometab.name should be NOT NULL\n# Failed test 120: "Column public.sometab.name should be NOT NULL"',
    'col_not_null( table, column ) should properly fail'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 120 );

/****************************************************************************/
-- Test col_is_null().
\echo ok 122 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'public', 'sometab', 'name', 'name is null' ),
    'ok 122 - name is null',
    'col_is_null( schema, table, column, desc ) should work'
);
\echo ok 124 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'public', 'sometab', 'name' ),
    'ok 124 - Column public.sometab.name should allow NULL',
    'col_is_null( schema, table, column ) should work'
);

\echo ok 126 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'sometab', 'name' ),
    'ok 126 - Column public.sometab.name should allow NULL',
    'col_is_null( table, column ) should work'
);
-- Make sure failure is correct.
\echo ok 128 - testing col_is_null( schema, table, column, desc )
SELECT is(
    col_is_null( 'sometab', 'id' ),
    E'not ok 128 - Column public.sometab.id should allow NULL\n# Failed test 128: "Column public.sometab.id should allow NULL"',
    'col_is_null( table, column ) should properly fail'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 128 );

/****************************************************************************/
-- Test col_type_is().
\echo ok 130 - testing col_type_is( schema, table, column, type, desc )
SELECT is(
    col_type_is( 'public', 'sometab', 'name', 'text', 'name is text' ),
    'ok 130 - name is text',
    'col_type_is( schema, table, column, type, desc ) should work'
);

\echo ok 132 - testing col_type_is( schema, table, column, type )
SELECT is(
    col_type_is( 'public', 'sometab', 'name', 'text' ),
    'ok 132 - Column public.sometab.name should be type text',
    'col_type_is( schema, table, column, type ) should work'
);

\echo ok 134 - testing col_type_is( table, column, type )
SELECT is(
    col_type_is( 'sometab', 'name', 'text' ),
    'ok 134 - Column public.sometab.name should be type text',
    'col_type_is( table, column, type ) should work'
);

\echo ok 136 - testing col_type_is( table, column, type ) case-insensitively
SELECT is(
    col_type_is( 'sometab', 'name', 'TEXT' ),
    'ok 136 - Column public.sometab.name should be type TEXT',
    'col_type_is( table, column, type ) should work case-insensitively'
);

-- Make sure failure is correct.
\echo ok 138 - testing col_type_is( table, column, type ) failure
SELECT is(
    col_type_is( 'sometab', 'name', 'int4' ),
    E'not ok 138 - Column public.sometab.name should be type int4\n# Failed test 138: "Column public.sometab.name should be type int4"\n#         have: text\n#         want: int4',
    'col_type_is( table, column, type ) should fail with proper diagnostics'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 138 );

/****************************************************************************/
-- Try col_type_is() with precision.
\echo ok 140 - testing col_type_is( schema, table, column, type(precision,scale), description )
SELECT is(
    col_type_is( 'public', 'sometab', 'numb', 'numeric(10,2)', 'lol' ),
    'ok 140 - lol',
    'col_type_is( schema, table, column, type, precision(scale,description) should work'
);

-- Check its diagnostics.
\echo ok 142 - col_type_is( schema, table, column, type, precision ) fail
SELECT is(
    col_type_is( 'public', 'sometab', 'myint', 'numeric(7)' ),
    E'not ok 142 - Column public.sometab.myint should be type numeric(7)\n# Failed test 142: "Column public.sometab.myint should be type numeric(7)"\n#         have: numeric(8,0)\n#         want: numeric(7)',
    'col_type_is with precision should have nice diagnostics'
);

UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 142, 158 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
