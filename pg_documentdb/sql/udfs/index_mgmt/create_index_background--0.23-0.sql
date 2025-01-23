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
/**
 * @ingroup index_mgmt
 * @brief Creates indexes on a collection in the background.
 *
 * @details This function processes the MongoDB `createIndexes` command asynchronously. It allows you to create one or more indexes on a specified collection without blocking other database operations. The indexes are created in the background by queueing the index builds, which are then processed by background workers.
 *
 * **Usage Examples:**
 * - Create a single index on a collection:
 *   ```sql
 *   SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "mycollection", "indexes": [{"key": {"field1": 1}, "name": "index1"}]}');
 *   ```
 * - Create multiple indexes on a collection:
 *   ```sql
 *   SELECT * FROM documentdb_api.create_indexes_background('db', '{"createIndexes": "mycollection", "indexes": [{"key": {"field1": 1}, "name": "index1"}, {"key": {"field2": -1}, "name": "index2"}]}');
 *   ```
 *

 * @param[in] p_database_name The name of the database where the indexes will be created. Must not be NULL.
 * @param[in] p_command A BSON document representing the `createIndexes` command, which includes:
 *   - `"createIndexes"`: The name of the collection on which to create the indexes. Must be a string.
 *   - `"indexes"`: An array of index specifications. Each specification must include:
 *     - `"key"`: An object specifying the fields to index and their sort order (`1` for ascending, `-1` for descending).
 *     - `"name"`: The name of the index.
 *     - Optional fields like `"unique"`, `"background"`, etc.
 *
 * @return A record containing:
 * - `retval` (documentdb_core.bson): A BSON document with the result of the operation, including any error messages.
 * - `ok` (boolean): Indicates whether the command was successful (`true`) or if an error occurred (`false`).
 * - `requests` (documentdb_core.bson): Details of the index build requests that have been queued.
 *
 * **Notes:**
 * - **Asynchronous Operation:** Index creation is performed asynchronously. The function returns immediately after queuing the index build requests.
 * - **Index Build Monitoring:** Use other system functions or views to monitor the progress of background index builds.
 * - **Limitations:**
 *   - The function does not block, so subsequent queries might not see the new indexes until they are built.
 *   - Index names must be unique within the collection.
 * - **Transaction Support:** This function does not support transactions since index creation is a schema-changing operation.
 *
 * **Related Functions:**
 * - `documentdb_api.create_indexes`: For creating indexes synchronously.
 * - `documentdb_api.list_indexes`: To list existing indexes on a collection.
 */
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