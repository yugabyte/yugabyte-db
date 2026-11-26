--
-- # Create a masking replica trigger for given table
--
-- ## Mandatory input parameters :
--
-- * `relint`: the table oid
-- * `tablename`: the schema-qualified table name (e.g. "MyApp"."MyTable" )
-- * `new_assignements` : the list of NEW value trigger will change
-- * `random`: a bit of entropy to avoid SQL injections
--
-- ## Example:
--
-- For a table named `foo` and oid 546458 with :
--   * a `fk_user` column masked with its md5 hash
--   * a `login` column masked with "CONFIDENTIAL"
--
-- then the trigger would look like this
--
-- CREATE OR REPLACE FUNCTION anon.replica_masking_546458()
-- RETURNS TRIGGER AS $func_483187318799514$
-- BEGIN
--   RAISE DEBUG '% Trigger fired on %.% with % => %',
--              TG_OP, TG_TABLE_SCHEMA, TG_TABLE_NAME, OLD, NEW;
--   NEW.fk_user = (SELECT CAST(pg_catalog.md5(fk_user) AS text) FROM (SELECT NEW.* ) AS n);
--   NEW.login = (SELECT CAST("CONFIDENTIAL") AS text);
--   RETURN NEW;
-- END;
-- $func_483187318799514$
-- LANGUAGE plpgsql;
--
--

-- This script may produce NOTICE messages that would be confusing for the user
SET client_min_messages TO WARNING;

CREATE OR REPLACE FUNCTION anon.replica_masking_{relint}()
RETURNS TRIGGER AS $func_{random}$
BEGIN
  RAISE DEBUG '% Trigger fired on %.% with % => %',
              TG_OP, TG_TABLE_SCHEMA, TG_TABLE_NAME, OLD, NEW;
  {new_assignments}
RETURN NEW;
END;
$func_{random}$
LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS tg_anon_replica_masking_{relint} ON {tablename};

CREATE TRIGGER tg_anon_replica_masking_{relint}
  BEFORE INSERT OR UPDATE ON {tablename}
  FOR EACH ROW
  EXECUTE FUNCTION anon.replica_masking_{relint}();

ALTER TABLE {tablename} ENABLE REPLICA TRIGGER tg_anon_replica_masking_{relint};
