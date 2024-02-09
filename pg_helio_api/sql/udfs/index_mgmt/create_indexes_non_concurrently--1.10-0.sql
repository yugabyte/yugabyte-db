/*
 * This is nearly same as calling __API_SCHEMA__.create_indexes() within a
 * transaction block.
 */
DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL__.create_indexes_non_concurrently;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.create_indexes_non_concurrently(
    IN p_database_name text,
    IN p_arg __CORE_SCHEMA__.bson,
    IN p_skip_check_collection_create boolean default FALSE)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_create_indexes_non_concurrently$$;