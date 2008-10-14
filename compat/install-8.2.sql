
-- Cast booleans to text like 8.3 does.
CREATE OR REPLACE FUNCTION booltext(boolean)
RETURNS text AS 'SELECT CASE WHEN $1 then ''true'' ELSE ''false'' END;'
LANGUAGE sql IMMUTABLE STRICT;

CREATE CAST (boolean AS text) WITH FUNCTION booltext(boolean) AS IMPLICIT;

-- Cast name[]s to text like 8.3 does.
CREATE OR REPLACE FUNCTION anyarray_text(anyarray)
RETURNS TEXT AS 'SELECT textin(array_out($1));'
LANGUAGE sql IMMUTABLE STRICT;

CREATE CAST (name[] AS text) WITH FUNCTION anyrray_text(anyarray) AS IMPLICIT;

-- Compare name[]s more or less like 8.3 does.
CREATE OR REPLACE FUNCTION namearray_eq( name[], name[] )
RETURNS bool
AS 'SELECT $1::text = $2::text;'
LANGUAGE sql IMMUTABLE STRICT;

CREATE OPERATOR = (
    LEFTARG    = name[],
    RIGHTARG   = name[],
    COMMUTATOR = <>,
    PROCEDURE  = namearray_eq
);

-- Cast regtypes to text like 8.3 does.
CREATE OR REPLACE FUNCTION regtypetext(regtype)
RETURNS text AS 'SELECT textin(regtypeout($1))'
LANGUAGE sql IMMUTABLE STRICT;

CREATE CAST (regtype AS text) WITH FUNCTION regtypetext(regtype) AS IMPLICIT;
