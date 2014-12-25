CREATE FUNCTION pg_catalog.reverse(str text)
RETURNS text
AS $$ SELECT plvstr.rvrs($1,1,NULL);$$
LANGUAGE SQL IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.reverse(text) IS 'Reverse string or part of string';

CREATE FUNCTION concat(text, text)
RETURNS text
AS 'MODULE_PATHNAME','ora_concat'
LANGUAGE C IMMUTABLE;
COMMENT ON FUNCTION concat(text, text) IS 'Concat two strings';

COMMIT;
