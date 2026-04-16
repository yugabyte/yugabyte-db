CREATE OR REPLACE PROCEDURE __API_SCHEMA_INTERNAL__.build_index_concurrently(IN p_job_index int)
 LANGUAGE C
AS 'MODULE_PATHNAME', $procedure$command_build_index_concurrently$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA_INTERNAL__.build_index_concurrently(int)
    IS 'Builds a index for a collection';

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.check_build_index_status(
    IN p_arg __CORE_SCHEMA__.bson,
    OUT retval __CORE_SCHEMA__.bson,
    OUT ok boolean,
    OUT complete boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_check_build_index_status$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.check_build_index_status(__CORE_SCHEMA__.bson)
    IS 'Calls check_build_index_status_internal.';

-- TODO: DROP the __API_SCHEMA__.create_indexes_background
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_indexes_background(
    p_database_name text, 
    p_index_spec __CORE_SCHEMA__.bson, 
    OUT retval __CORE_SCHEMA__.bson,
    OUT ok boolean,
    OUT requests __CORE_SCHEMA__.bson)
RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_create_indexes_background$$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.create_indexes_background(text, __CORE_SCHEMA__.bson)
    IS 'Submits the build index(es) requests on a collection and waits for them to finish.';

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.check_build_index_status_internal(
    IN p_arg __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_check_build_index_status_internal$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.check_build_index_status_internal(__CORE_SCHEMA__.bson)
    IS 'Checks for build index(es) requests to finish.';

DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL__.create_indexes_background_internal CASCADE;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.create_indexes_background_internal(
    IN p_database_name text,
    IN p_arg __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_create_indexes_background_internal$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.create_indexes_background_internal(text,__CORE_SCHEMA__.bson)
    IS 'Queues the Index creation request(s) on a collection';

DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL__.schedule_background_index_build_workers;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.schedule_background_index_build_jobs(IN p_force_override boolean DEFAULT false)
RETURNS void
LANGUAGE C
AS 'MODULE_PATHNAME', $$schedule_background_index_build_jobs$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.schedule_background_index_build_jobs(boolean)
    IS 'Schedules the background index build cron job.';