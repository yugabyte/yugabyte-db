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

CREATE OR REPLACE FUNCTION oracle.nvl(bigint, int)
RETURNS bigint AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.nvl(numeric, int)
RETURNS numeric AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.add_months(TIMESTAMP WITH TIME ZONE,INTEGER)
RETURNS TIMESTAMP
AS $$ SELECT (pg_catalog.add_months($1::pg_catalog.date, $2) + $1::time)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

drop view oracle.user_tables;

create view oracle.user_tables as
    select table_name
      from information_schema.tables
     where table_type = 'BASE TABLE';

-- new functionality
CREATE FUNCTION oracle.orafce_concat2(varchar2, varchar2)
RETURNS varchar2
AS 'MODULE_PATHNAME','orafce_concat2'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION oracle.orafce_concat2(nvarchar2, nvarchar2)
RETURNS nvarchar2
AS 'MODULE_PATHNAME','orafce_concat2'
LANGUAGE C STABLE;

CREATE OPERATOR || (procedure = oracle.orafce_concat2, leftarg = varchar2, rightarg = varchar2);
CREATE OPERATOR || (procedure = oracle.orafce_concat2, leftarg = nvarchar2, rightarg = nvarchar2);

CREATE OR REPLACE FUNCTION oracle.numtodsinterval(double precision, text)
RETURNS interval AS $$
  SELECT $1 * ('1' || $2)::interval
$$ LANGUAGE sql IMMUTABLE STRICT;
