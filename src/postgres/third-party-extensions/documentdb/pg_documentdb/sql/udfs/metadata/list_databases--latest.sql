CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.list_databases(
    p_list_databases_spec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $$command_list_databases$$;