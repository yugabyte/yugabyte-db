CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.record_id_index(
    p_collection_id bigint)
 RETURNS VOID
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_record_id_index$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.record_id_index(bigint)
    IS 'record built-in "_id" to index metadata';