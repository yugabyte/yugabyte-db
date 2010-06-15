CREATE FUNCTION dump(text) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

CREATE FUNCTION dump(text, integer) 
RETURNS varchar
AS 'MODULE_PATHNAME', 'orafce_dump'
LANGUAGE C;

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
