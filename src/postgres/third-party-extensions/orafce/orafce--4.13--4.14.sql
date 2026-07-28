CREATE OR REPLACE FUNCTION oracle.nvl(double precision, int)
RETURNS double precision AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE PARALLEL SAFE;

CREATE FUNCTION oracle.to_date(integer, TEXT)
RETURNS oracle.date
AS $$ SELECT oracle.orafce__obsolete_to_date($1::text, $2)::oracle.date; $$
LANGUAGE SQL STABLE STRICT PARALLEL SAFE;

