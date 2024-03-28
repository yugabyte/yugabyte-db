-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pg_trgm UPDATE TO '1.6-yb-1.0'" to load this file. \quit

/* Sourced from contrib/pg_trgm/pg_trgm--1.4--1.5.sql */

CREATE FUNCTION gtrgm_options(internal)
RETURNS void
AS 'MODULE_PATHNAME', 'gtrgm_options'
LANGUAGE C IMMUTABLE PARALLEL SAFE;

ALTER OPERATOR FAMILY gist_trgm_ops USING gist
ADD FUNCTION 10 (text) gtrgm_options (internal);

ALTER OPERATOR % (text, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR <% (text, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR %> (text, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR <<% (text, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);
ALTER OPERATOR %>> (text, text)
  SET (RESTRICT = matchingsel, JOIN = matchingjoinsel);

/* Sourced from contrib/pg_trgm/pg_trgm--1.5--1.6.sql */

ALTER OPERATOR FAMILY gin_trgm_ops USING gin ADD
        OPERATOR        11       pg_catalog.= (text, text);

ALTER OPERATOR FAMILY gist_trgm_ops USING gist ADD
        OPERATOR        11       pg_catalog.= (text, text);

/*
 * Translate gin operator class to ybgin operator class.
 */

/* Adopted from contrib/pg_trgm/pg_trgm--1.5--1.6.sql */

ALTER OPERATOR FAMILY gin_trgm_ops USING ybgin ADD
        OPERATOR        11       pg_catalog.= (text, text);
