--
-- Yugabyte created functions
--

-- directory path and dlsuffix are passed to us in environment variables
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

CREATE FUNCTION bigname_in(cstring)
   RETURNS bigname
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION bigname_out(bigname)
   RETURNS cstring
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION yb_run_spi(text, int)
   RETURNS int8
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION yb_cacheinfo()
   RETURNS TABLE (
      cache_id int,
      name text,
      reloid oid,
      indoid oid,
      nkeys int,
      key1 int,
      key2 int,
      key3 int,
      key4 int,
      nbuckets int
   )
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION catalog_cache_compute_hash_tuple(record)
   RETURNS bigint
   AS :'regresslib'
   LANGUAGE C STRICT IMMUTABLE;
