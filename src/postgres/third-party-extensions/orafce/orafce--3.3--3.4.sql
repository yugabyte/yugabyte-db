/*
 * Undocumented function wm_concat - removed from
 * Oracle 12c.
 */
CREATE FUNCTION pg_catalog.wm_concat_transfn(internal, text)
RETURNS internal
AS 'MODULE_PATHNAME','orafce_wm_concat_transfn'
LANGUAGE C IMMUTABLE;

CREATE AGGREGATE pg_catalog.wm_concat(text) (
  SFUNC=pg_catalog.wm_concat_transfn,
  STYPE=internal,
  FINALFUNC=pg_catalog.listagg_finalfn
);
