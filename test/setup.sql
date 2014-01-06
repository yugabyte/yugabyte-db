\set QUIET 1

--
-- Tests for pgTAP.
--
--
-- Format the output for nice TAP.
\pset format unaligned
\pset tuples_only true
\pset pager

-- Revert all changes on failure.
\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;

-- Uncomment when testing with PGOPTIONS=--search_path=tap
-- CREATE SCHEMA tap; SET search_path TO tap,public;

-- Load the TAP functions.
\i sql/pgtap.sql
