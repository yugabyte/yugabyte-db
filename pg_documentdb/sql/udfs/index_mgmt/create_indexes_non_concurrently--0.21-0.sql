/*
 * This is nearly same as calling __API_SCHEMA__.create_indexes() within a
 * transaction block.
 */
DROP FUNCTION IF EXISTS helio_api_internal.create_indexes_non_concurrently;
CREATE OR REPLACE FUNCTION helio_api_internal.create_indexes_non_concurrently(
    IN p_database_name text,
    IN p_arg helio_core.bson,
    IN p_skip_check_collection_create boolean default FALSE)
 RETURNS helio_core.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_create_indexes_non_concurrently$$;