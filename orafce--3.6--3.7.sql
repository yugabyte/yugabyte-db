CREATE OR REPLACE FUNCTION oracle.numtodsinterval(double precision, text)
RETURNS interval AS $$
  SELECT $1 * ('1' || $2)::interval
$$ LANGUAGE sql IMMUTABLE STRICT;

-- bugfixes

GRANT USAGE ON SCHEMA oracle TO PUBLIC;
GRANT USAGE ON SCHEMA plunit TO PUBLIC;

CREATE OR REPLACE FUNCTION oracle.round(float4, int)
RETURNS numeric
AS $$SELECT pg_catalog.round($1::numeric, $2)$$
LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.trunc(float4, int)
RETURNS numeric
AS $$SELECT pg_catalog.trunc($1::numeric, $2)$$
LANGUAGE sql IMMUTABLE STRICT;

COMMENT ON FUNCTION oracle.sessiontimezone() IS 'Ruturns session time zone';
COMMENT ON FUNCTION oracle.dbtimezone() IS 'Ruturns server time zone (orafce.timezone)';

CREATE FUNCTION concat2(varchar2, varchar2)
RETURNS varchar2 AS $$
SELECT pg_catalog.concat($1::text, $2::text)
$$ LANGUAGE sql;

CREATE FUNCTION concat2(nvarchar2, nvarchar2)
RETURNS varchar2 AS $$
SELECT pg_catalog.concat($1::text, $2::text)
$$ LANGUAGE sql;

CREATE OPERATOR || (function = concat2, leftarg = varchar2, rightarg = varchar2);
CREATE OPERATOR || (function = concat2, leftarg = nvarchar2, rightarg = nvarchar2);
