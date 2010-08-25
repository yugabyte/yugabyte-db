CREATE FUNCTION dump(text) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

CREATE FUNCTION dump(text, integer) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

CREATE FUNCTION utl_file.put_line(file utl_file.file_type, buffer anyelement)
RETURNS bool
AS $$SELECT utl_file.put_line($1, $2::text); $$
LANGUAGE SQL VOLATILE;
COMMENT ON FUNCTION utl_file.put_line(utl_file.file_type, anyelement) IS 'Puts data to specified file and append newline character';

CREATE FUNCTION utl_file.put_line(file utl_file.file_type, buffer anyelement, autoflush bool)
RETURNS bool
AS $$SELECT utl_file.put_line($1, $2::text, true); $$
LANGUAGE SQL VOLATILE;
COMMENT ON FUNCTION utl_file.put_line(utl_file.file_type, anyelement, bool) IS 'Puts data to specified file and append newline character';

CREATE FUNCTION pg_catalog.listagg1_transfn(internal, text)
RETURNS internal 
AS 'MODULE_PATHNAME','orafce_listagg1_transfn'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_catalog.listagg2_transfn(internal, text, text)
RETURNS internal 
AS 'MODULE_PATHNAME','orafce_listagg2_transfn'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_catalog.listagg_finalfn(internal)
RETURNS text
AS 'MODULE_PATHNAME','orafce_listagg_finalfn'
LANGUAGE C IMMUTABLE;

CREATE AGGREGATE pg_catalog.listagg(text) (
  SFUNC=pg_catalog.listagg1_transfn, 
  STYPE=internal, 
  FINALFUNC=pg_catalog.listagg_finalfn
);

CREATE AGGREGATE pg_catalog.listagg(text, text) (
  SFUNC=pg_catalog.listagg2_transfn, 
  STYPE=internal, 
  FINALFUNC=pg_catalog.listagg_finalfn
);

CREATE FUNCTION pg_catalog.median4_transfn(internal, real)
RETURNS internal
AS 'MODULE_PATHNAME','orafce_median4_transfn'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_catalog.median4_finalfn(internal)
RETURNS real
AS 'MODULE_PATHNAME','orafce_median4_finalfn'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_catalog.median8_transfn(internal, double precision)
RETURNS internal
AS 'MODULE_PATHNAME','orafce_median8_transfn'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_catalog.median8_finalfn(internal)
RETURNS double precision
AS 'MODULE_PATHNAME','orafce_median8_finalfn'
LANGUAGE C IMMUTABLE;

CREATE AGGREGATE pg_catalog.median(real) (
  SFUNC=pg_catalog.median4_transfn, 
  STYPE=internal, 
  FINALFUNC=pg_catalog.median4_finalfn
);

CREATE AGGREGATE pg_catalog.median(double precision) (
  SFUNC=pg_catalog.median8_transfn, 
  STYPE=internal, 
  FINALFUNC=pg_catalog.median8_finalfn
);

COMMIT;
