-- remove obsolete signature of dbms_alert.defered_signal
DO $$
BEGIN
  IF EXISTS (SELECT *
               FROM pg_proc p
                    JOIN pg_namespace n ON p.pronamespace = n.oid
             WHERE n.nspname = 'dbms_alert' AND p.proname = 'defered_signal') THEN
    DROP FUNCTION dbms_alert.defered_signal();
  END IF;
END;
$$;

CREATE FUNCTION oracle.orafce__obsolete_to_date(str text, fmt text)
RETURNS timestamp
AS 'MODULE_PATHNAME','ora_to_date'
LANGUAGE C STABLE STRICT PARALLEL SAFE;

CREATE OR REPLACE FUNCTION oracle.to_date(TEXT, TEXT)
RETURNS oracle.date
AS $$ SELECT oracle.orafce__obsolete_to_date($1, $2)::oracle.date; $$
LANGUAGE SQL STABLE STRICT PARALLEL SAFE;

