-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION hstore UPDATE TO '1.8-yb-1.0'" to load this file. \quit

/* Sourced from contrib/hstore/hstore--1.5--1.6.sql */

CREATE FUNCTION hstore_hash_extended(hstore, int8)
RETURNS int8
AS 'MODULE_PATHNAME','hstore_hash_extended'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

ALTER OPERATOR FAMILY hash_hstore_ops USING hash ADD
    FUNCTION    2   hstore_hash_extended(hstore, int8);

/* Sourced from contrib/hstore/hstore--1.6--1.7.sql */

CREATE FUNCTION ghstore_options(internal)
RETURNS void
AS 'MODULE_PATHNAME', 'ghstore_options'
LANGUAGE C IMMUTABLE PARALLEL SAFE;

ALTER OPERATOR FAMILY gist_hstore_ops USING gist
ADD FUNCTION 10 (hstore) ghstore_options (internal);

ALTER OPERATOR ? (hstore, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR ?| (hstore, text[])
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR ?& (hstore, text[])
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR @> (hstore, hstore)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR <@ (hstore, hstore)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR @ (hstore, hstore)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR ~ (hstore, hstore)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);

/* Sourced from contrib/hstore/hstore--1.7--1.8.sql */

CREATE FUNCTION hstore_subscript_handler(internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'hstore_subscript_handler'
LANGUAGE C STRICT IMMUTABLE PARALLEL SAFE;

ALTER TYPE hstore SET (
  SUBSCRIPT = hstore_subscript_handler
);

-- Remove @ and ~
DROP OPERATOR @ (hstore, hstore);
DROP OPERATOR ~ (hstore, hstore);
