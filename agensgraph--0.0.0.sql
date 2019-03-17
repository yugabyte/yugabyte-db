-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION agensgraph" to load this file. \quit

CREATE FUNCTION "cypher"(text)
RETURNS SETOF "record"
LANGUAGE "c"
AS 'MODULE_PATHNAME';
