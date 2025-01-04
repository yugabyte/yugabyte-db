CREATE OR REPLACE PROCEDURE __API_SCHEMA_INTERNAL__.build_index_concurrently(IN p_job_index int)
 LANGUAGE C
AS 'MODULE_PATHNAME', $procedure$command_build_index_concurrently$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA_INTERNAL__.build_index_concurrently(int)
    IS 'Builds a index for a Mongo collection';

CREATE OR REPLACE FUNCTION helio_api_internal.check_build_index_status(
    IN p_arg __CORE_SCHEMA__.bson,
    OUT retval __CORE_SCHEMA__.bson,
    OUT ok boolean,
    OUT complete boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_check_build_index_status$$;
COMMENT ON FUNCTION helio_api_internal.check_build_index_status(__CORE_SCHEMA__.bson)
    IS 'Calls check_build_index_status_internal using run_command_on_coordinator.';

-- TODO: DROP the __API_SCHEMA__.create_indexes_background
CREATE OR REPLACE FUNCTION helio_api.create_indexes_background(
    p_database_name text, 
    p_index_spec __CORE_SCHEMA__.bson, 
    OUT retval __CORE_SCHEMA__.bson,
    OUT ok boolean,
    OUT requests __CORE_SCHEMA__.bson)
RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_create_indexes_background$$;
COMMENT ON FUNCTION helio_api.create_indexes_background(text, __CORE_SCHEMA__.bson)
    IS 'Submits the build index(es) requests on a Mongo Collection and waits for them to finish.';