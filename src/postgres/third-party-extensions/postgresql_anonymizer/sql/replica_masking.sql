SET search_path='';

CREATE OR REPLACE FUNCTION anon.start_replica_masking(
  policy TEXT DEFAULT 'anon'
)
RETURNS BOOLEAN AS
$$
  SELECT anon.refresh_replica_masking(policy)
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because of CREATE TRIGGER
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.start_replica_masking(TEXT) IS 'UNTRUSTED';

-- Walk through all tables with masked columns and
-- update the replica masking triggers
CREATE OR REPLACE FUNCTION anon.refresh_replica_masking(
  policy TEXT DEFAULT 'anon'
)
RETURNS BOOLEAN AS
$$
  SELECT anon.stop_replica_masking();
  SELECT bool_or(anon.refresh_replica_trigger_for_table(t.regclass,policy))
  FROM (
      SELECT distinct attrelid::REGCLASS as regclass
      FROM anon.pg_masking_rules
  ) as t;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because of CREATE TRIGGER
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.refresh_replica_masking(TEXT) IS 'UNTRUSTED';


--
-- The Event trigger below works fine (almost). But since the
-- `anon.refresh_replica_trigger_for_table` is not transactionnal the event
-- trigger won't work within a long transaction....
--
--
-- We're keeping this while waiting for a way to remove the SPI calls.
--
-- --
-- --
-- -- This is run after any SECURITY LABEL statement
-- --
-- CREATE OR REPLACE FUNCTION anon.tg_refresh_replica_masking()
-- RETURNS EVENT_TRIGGER AS
-- $$
-- DECLARE
--     seclabel_on_column_event RECORD;
-- BEGIN
--     FOR seclabel_on_column_event IN
--         SELECT *
--         FROM pg_catalog.pg_event_trigger_ddl_commands()
--         WHERE classid = 'pg_class'::REGCLASS::OID
--           AND objsubid IS NOT NULL                -- The SECLABEL is on a column
--           AND NOT e.in_extension                  -- Ignore the init script
--           AND pg_catalog.current_setting(anon.replica_masking)
--     LOOP
--       RAISE DEBUG 'Anon: Refreshing replica maskign trigger for %', seclabel_on_column_event.object_identity;
--       PERFORM anon.refresh_replica_trigger_for_table(seclabel_on_column_event.objid,'anon');
--     END LOOP;
-- END
-- $$
--   LANGUAGE plpgsql
--   PARALLEL UNSAFE -- because of UPDATE
--   SECURITY INVOKER
--   SET search_path=''
-- ;
--
-- SECURITY LABEL FOR anon ON FUNCTION anon.tg_refresh_replica_masking IS 'UNTRUSTED';
--
--
-- CREATE EVENT TRIGGER anon_tg_refresh_replica_masking
--   ON ddl_command_end
--   WHEN TAG IN ( 'SECURITY LABEL' )
--   EXECUTE PROCEDURE anon.tg_refresh_replica_masking()
-- ;



-- Walk through all triggers and function we created previously
-- and drop everything
CREATE OR REPLACE FUNCTION anon.stop_replica_masking()
RETURNS BOOLEAN AS
$$
DECLARE
    trigger RECORD;
    function RECORD;
BEGIN
    FOR trigger IN
        SELECT
            t.trigger_name,
            t.event_object_table,
            t.event_object_schema
        FROM information_schema.triggers t
        WHERE t.trigger_name LIKE 'tg_anon_replica_masking_%'
    LOOP
        EXECUTE pg_catalog.format('DROP TRIGGER IF EXISTS %I ON %I.%I',
            trigger.trigger_name,
            trigger.event_object_schema,
            trigger.event_object_table
        );
    END LOOP;

    FOR function IN
        SELECT
            p.proname as function_name,
            n.nspname as schema_name,
            pg_catalog.pg_get_function_identity_arguments(p.oid) as function_args
        FROM pg_proc p
        JOIN pg_namespace n ON p.pronamespace = n.oid
        WHERE n.nspname = 'anon'
        AND p.proname LIKE 'replica%'
    LOOP
        EXECUTE pg_catalog.format('DROP FUNCTION IF EXISTS %I.%I(%s)',
            function.schema_name,
            function.function_name,
            function.function_args
        );
    END LOOP;

    RETURN TRUE;
END;
$$
  LANGUAGE plpgsql
  VOLATILE
  PARALLEL UNSAFE -- because of DROP TRIGGER
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.stop_replica_masking() IS 'UNTRUSTED';

RESET search_path;
