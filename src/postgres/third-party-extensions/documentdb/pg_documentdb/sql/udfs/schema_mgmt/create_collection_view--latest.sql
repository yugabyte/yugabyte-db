

/*
 * __API_SCHEMA_V2__.create_collection_view processes a Mongo wire protocol create command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_collection_view(dbname text, createSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $$command_create_collection_view$$;