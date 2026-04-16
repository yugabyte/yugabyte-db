CREATE FUNCTION dbms_alert.waitany(OUT name text, OUT message text, OUT status integer)
RETURNS record
AS 'MODULE_PATHNAME','dbms_alert_waitany_maxwait'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.waitany(OUT text, OUT text, OUT integer) IS 'Wait for any signal';

CREATE FUNCTION dbms_alert.waitone(name text, OUT message text, OUT status integer)
RETURNS record
AS 'MODULE_PATHNAME','dbms_alert_waitone_maxwait'
LANGUAGE C VOLATILE;
COMMENT ON FUNCTION dbms_alert.waitone(text, OUT text, OUT integer) IS 'Wait for specific signal';
