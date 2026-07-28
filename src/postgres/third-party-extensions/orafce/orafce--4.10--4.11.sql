CREATE FUNCTION oracle.sys_extract_utc(timestamp with time zone)
RETURNS timestamp without time zone
AS 'MODULE_PATHNAME','orafce_sys_extract_utc'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION oracle.sysdate() IS 'Extract timestamp at utc time zone';

CREATE FUNCTION oracle.sys_extract_utc(oracle.date)
RETURNS oracle.date
AS 'MODULE_PATHNAME','orafce_sys_extract_utc_oracle_date'
LANGUAGE C STABLE STRICT PARALLEL SAFE;
COMMENT ON FUNCTION oracle.sysdate() IS 'Extract timestamp at utc time zone';
