/* contrib/hstore/hstore--1.5--1.5-yb-1.0.sql */

-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION hstore UPDATE TO '1.5-yb-1.0'" to load this file. \quit

/*
 * Translate gin operator class to ybgin operator class.
 */

/* Adopted from contrib/hstore/hstore--1.4.sql */

CREATE OPERATOR CLASS gin_hstore_ops
DEFAULT FOR TYPE hstore USING ybgin
AS
	OPERATOR        7       @>,
	OPERATOR        9       ?(hstore,text),
	OPERATOR        10      ?|(hstore,text[]),
	OPERATOR        11      ?&(hstore,text[]),
	FUNCTION        1       bttextcmp(text,text),
	FUNCTION        2       gin_extract_hstore(hstore, internal),
	FUNCTION        3       gin_extract_hstore_query(hstore, internal, int2, internal, internal),
	FUNCTION        4       gin_consistent_hstore(internal, int2, hstore, int4, internal, internal),
	STORAGE         text;
