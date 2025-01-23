/* db.collection.drop(writeConcern) */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.drop_collection CASCADE;
/**
 * @ingroup schema_mgmt
 * @brief Drops an existing collection from the DocumentDB database.
 *
 * @details This function removes the specified collection from the target database. 
 *          Optionally, a write concern, collection UUID, and change tracking settings can be provided 
 *          to control the behavior of the drop operation.
 *
 * **Usage Examples:**
 * - Drop an existing collection without additional parameters:
 *   ```sql
 *   SELECT documentdb_api.drop_collection('hr_database', 'employees');
 *   -- Returns:
 *   -- true
 *   ```
 *
 * - Drop a collection with a specific write concern:
 *   ```sql
 *   SELECT documentdb_api.drop_collection(
 *       'hr_database',
 *       'employees',
 *       '{"w": "majority"}');
 *   -- Returns:
 *   -- true
 *   ```
 *
 * - Drop a collection using its UUID:
 *   ```sql
 *   SELECT documentdb_api.drop_collection(
 *       'hr_database',
 *       'employees',
 *       NULL,
 *       '123e4567-e89b-12d3-a456-426614174000');
 *   -- Returns:
 *   -- true
 *   ```
 *
 * - Drop a collection and disable change tracking:
 *   ```sql
 *   SELECT documentdb_api.drop_collection(
 *       'hr_database',
 *       'employees',
 *       NULL,
 *       NULL,
 *       false);
 *   -- Returns:
 *   -- true
 *   ```
 *

 * @param[in] p_database_name Name of the target database from which the collection will be dropped. Must not be NULL.
 * @param[in] p_collection_name Name of the collection to drop. Must not be NULL.
 * @param[in] p_write_concern (Optional) BSON object specifying the write concern for the operation. Defaults to NULL.
 * @param[in] p_collection_uuid (Optional) UUID of the collection to drop. Defaults to NULL.
 * @param[in] p_track_changes (Optional) Boolean indicating whether changes should be tracked. Defaults to true.
 *
 * **Returns:**
 * - `true` if the collection is dropped successfully.
 * - `false` if the operation fails.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.drop_collection(
    p_database_name text,
    p_collection_name text,
    p_write_concern __CORE_SCHEMA_V2__.bson default null,
    p_collection_uuid uuid default null,
    p_track_changes bool default true)
RETURNS bool
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_drop_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.drop_collection(text,text,__CORE_SCHEMA_V2__.bson,uuid,bool)
    IS 'drop a collection';
