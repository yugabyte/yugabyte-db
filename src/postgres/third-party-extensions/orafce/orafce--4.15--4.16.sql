CREATE FUNCTION oracle.months_between(date1 oracle.date, date2 oracle.date)
RETURNS numeric
AS 'MODULE_PATHNAME', 'months_between_timestamp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION oracle.months_between(oracle.date, oracle.date) IS 'returns the number of months between date1 and date2';

CREATE OR REPLACE FUNCTION oracle.months_between(TIMESTAMP WITH TIME ZONE,TIMESTAMP WITH TIME ZONE)
RETURNS NUMERIC
AS 'MODULE_PATHNAME', 'months_between_timestamptz'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION oracle.months_between(TIMESTAMP WITH TIME ZONE, TIMESTAMP WITH TIME ZONE) IS 'returns the number of months between date1 and date2';
