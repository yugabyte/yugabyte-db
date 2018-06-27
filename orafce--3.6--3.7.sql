CREATE OR REPLACE FUNCTION oracle.numtodsinterval(double precision, text)
RETURNS interval AS $$
  SELECT $1 * ('1' || $2)::interval
$$ LANGUAGE sql IMMUTABLE STRICT;

GRANT USAGE ON SCHEMA oracle TO PUBLIC;
GRANT USAGE ON SCHEMA plunit TO PUBLIC;
