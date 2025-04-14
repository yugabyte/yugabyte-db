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
    IS 'Calls check_build_index_status_internal using run_command_on_coordinator.';

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

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.schedule_background_index_build_workers(p_max_num_active_user_index_builds int default current_setting(__SINGLE_QUOTED_STRING__(__API_GUC_PREFIX__) || '.maxNumActiveUsersIndexBuilds')::int,
p_user_index_build_schedule int default current_setting(__SINGLE_QUOTED_STRING__(__API_GUC_PREFIX__) || '.indexBuildScheduleInSec')::int)
RETURNS void
AS $fn$
DECLARE
    v_indexBuildScheduleInterval text;
BEGIN
    IF citus_is_coordinator() THEN
        SELECT '* * * * *' INTO v_indexBuildScheduleInterval;
        IF p_user_index_build_schedule < 60 THEN
            SELECT p_user_index_build_schedule || ' seconds' INTO v_indexBuildScheduleInterval;
        END IF;
        
        PERFORM cron.unschedule(jobid) FROM cron.job WHERE jobname LIKE __SINGLE_QUOTED_STRING__(__EXTENSION_OBJECT_PREFIX_V2__) || '_index_build_task_%'; 
    
        FOR i IN 1..p_max_num_active_user_index_builds LOOP
            PERFORM cron.schedule(__SINGLE_QUOTED_STRING__(__EXTENSION_OBJECT_PREFIX_V2__) || '_index_build_task_' || i, v_indexBuildScheduleInterval, format($$ CALL __API_SCHEMA_INTERNAL__.build_index_concurrently(%s) $$, i));
        END LOOP;
    ELSE
        RAISE EXCEPTION 'schedule_background_index_build_workers() to be run on coordinator only';
    END IF;
END;
$fn$ LANGUAGE plpgsql;