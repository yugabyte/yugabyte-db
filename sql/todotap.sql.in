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
SELECT plan(3);

/****************************************************************************/
-- Test todo tests.
\echo ok 1 - todo fail
\echo ok 2 - todo pass
SELECT * FROM todo('just because', 2 );
SELECT is(
    fail('This is a todo test' ) || E'\n'
      || pass('This is a todo test that unexpectedly passes' ),
    E'not ok 1 - This is a todo test # TODO just because\n# Failed (TODO) test 1: "This is a todo test"\nok 2 - This is a todo test that unexpectedly passes # TODO just because',
   'TODO tests should display properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 2 );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
