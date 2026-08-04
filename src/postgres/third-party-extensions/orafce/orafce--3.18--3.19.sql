CREATE OR REPLACE FUNCTION pg_catalog.substrb(varchar2, integer, integer) RETURNS varchar2
AS 'MODULE_PATHNAME','oracle_substrb3' LANGUAGE C STRICT IMMUTABLE;

CREATE OR REPLACE FUNCTION pg_catalog.substrb(varchar2, integer) RETURNS varchar2
AS 'MODULE_PATHNAME','oracle_substrb2' LANGUAGE C STRICT IMMUTABLE;

DROP FUNCTION public.nvl2(anyelement, anyelement, anyelement);

CREATE FUNCTION public.nvl2("any", anyelement, anyelement)
RETURNS anyelement
AS 'MODULE_PATHNAME','ora_nvl2'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION public.nvl2("any", text, text)
RETURNS text
AS 'MODULE_PATHNAME','ora_nvl2'
LANGUAGE C IMMUTABLE;
