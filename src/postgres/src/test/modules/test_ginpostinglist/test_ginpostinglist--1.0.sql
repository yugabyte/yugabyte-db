/* src/test/modules/test_ginpostinglist/test_ginpostinglist--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_ginpostinglist" to load this file. \quit

CREATE FUNCTION test_ginpostinglist()
RETURNS pg_catalog.void STRICT
AS 'MODULE_PATHNAME' LANGUAGE C;
