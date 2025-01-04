

/*
 * helio_api.create_collection_view processes a Mongo wire protocol create command.
 */
CREATE OR REPLACE FUNCTION helio_api.create_collection_view(dbname text, createSpec helio_core.bson)
 RETURNS helio_core.bson
 LANGUAGE c
 VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $$command_create_collection_view$$;