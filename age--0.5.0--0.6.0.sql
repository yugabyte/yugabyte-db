-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION age UPDATE TO '0.6.0'" to load this file. \quit

CREATE OR REPLACE FUNCTION ag_catalog.age_vle(
    IN agtype, IN agtype, IN agtype, IN agtype,
    IN agtype, IN agtype, IN agtype,
    OUT edges agtype)
RETURNS SETOF agtype
LANGUAGE C
IMMUTABLE
STRICT
AS 'MODULE_PATHNAME';
