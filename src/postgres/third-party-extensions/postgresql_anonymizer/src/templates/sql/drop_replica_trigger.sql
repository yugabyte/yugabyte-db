--
-- # Drop the masking replica trigger for given table
--
-- ## Mandatory input parameters :
--
-- * `relint`: the table oid
-- * `tablename`: the schema-qualified table name (e.g. "MyApp"."MyTable" )
--

-- This may produce NOTICE messages that would be confusing for the user
SET client_min_messages TO WARNING;

DROP TRIGGER IF EXISTS tg_anon_replica_masking_{relint} ON {tablename} CASCADE;

DROP FUNCTION IF EXISTS anon.replica_masking_{relint}() CASCADE;
