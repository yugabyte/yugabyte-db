/* contrib/pg_trgm/pg_trgm--1.4--1.4-yb-1.0.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pg_trgm UPDATE TO '1.4-yb-1.0'" to load this file. \quit

/*
 * Translate gin_trgm_ops to ybgin_trgm_ops.  Ignore gist because ybgist index
 * access method does not exist, yet.
 */

/* Adopted from contrib/pg_trgm/pg_trgm--1.3.sql */

-- create the operator class for ybgin
CREATE OPERATOR CLASS gin_trgm_ops
FOR TYPE text USING ybgin
AS
        OPERATOR        1       % (text, text),
        FUNCTION        1       btint4cmp (int4, int4),
        FUNCTION        2       gin_extract_value_trgm (text, internal),
        FUNCTION        3       gin_extract_query_trgm (text, internal, int2, internal, internal, internal, internal),
        FUNCTION        4       gin_trgm_consistent (internal, int2, text, int4, internal, internal, internal, internal),
        STORAGE         int4;

-- Add operators that are new in 9.1.

ALTER OPERATOR FAMILY gin_trgm_ops USING ybgin ADD
        OPERATOR        3       pg_catalog.~~ (text, text),
        OPERATOR        4       pg_catalog.~~* (text, text);

-- Add operators that are new in 9.3.

ALTER OPERATOR FAMILY gin_trgm_ops USING ybgin ADD
        OPERATOR        5       pg_catalog.~ (text, text),
        OPERATOR        6       pg_catalog.~* (text, text);

-- Add functions that are new in 9.6 (pg_trgm 1.2).

ALTER OPERATOR FAMILY gin_trgm_ops USING ybgin ADD
        OPERATOR        7       %> (text, text),
        FUNCTION        6      (text,text) gin_trgm_triconsistent (internal, int2, text, int4, internal, internal, internal);

/* Adopted from contrib/pg_trgm/pg_trgm--1.3--1.4.sql */

ALTER OPERATOR FAMILY gin_trgm_ops USING ybgin ADD
        OPERATOR        9       %>> (text, text);
