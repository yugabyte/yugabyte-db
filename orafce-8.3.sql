CREATE FUNCTION pg_catalog.reverse(str text)
RETURNS text
AS $$ SELECT plvstr.rvrs($1,1,NULL);$$
LANGUAGE SQL IMMUTABLE STRICT;
COMMENT ON FUNCTION pg_catalog.reverse(text) IS 'Reverse string or part of string';

CREATE FUNCTION dump(text) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

CREATE FUNCTION dump(text, integer) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

CREATE FUNCTION concat(text, text)
RETURNS text
AS 'MODULE_PATHNAME','ora_concat'
LANGUAGE C IMMUTABLE;
COMMENT ON FUNCTION concat(text, text) IS 'Concat two strings';

CREATE FUNCTION concat(text, anyarray)
RETURNS text
AS 'SELECT concat($1, $2::text)'
LANGUAGE sql IMMUTABLE;

CREATE FUNCTION concat(anyarray, text)
RETURNS text
AS 'SELECT concat($1::text, $2)'
LANGUAGE sql IMMUTABLE;

CREATE FUNCTION concat(anyarray, anyarray)
RETURNS text
AS 'SELECT concat($1::text, $2::text)'
LANGUAGE sql IMMUTABLE;

CREATE FUNCTION concat(text, anynonarray)
RETURNS text
AS 'SELECT concat($1, $2::text)'
LANGUAGE sql IMMUTABLE;

CREATE FUNCTION concat(anynonarray, text)
RETURNS text
AS 'SELECT concat($1::text, $2)'
LANGUAGE sql IMMUTABLE;

CREATE FUNCTION concat(anynonarray, anynonarray)
RETURNS text
AS 'SELECT concat($1::text, $2::text)'
LANGUAGE sql IMMUTABLE;

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

COMMIT;
