/* API that renames a collection given a database and a collection to the specified target name. */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.rename_collection;
/**
 * @ingroup schema_mgmt
 * @brief Renames an existing collection in the DocumentDB database.
 *
 * @details This function renames a collection within the specified database. Optionally, a boolean 
 *          flag can be provided to drop the target collection if it already exists before renaming.
 *
 * **Usage Examples:**
 * - Rename a collection without dropping the target collection:
 *   ```sql
 *   SELECT documentdb_api.rename_collection('hr_database', 'employees', 'staff');
 *   -- Returns:
 *   --
 *   ```
 *
 * - Rename a collection and drop the target collection if it exists:
 *   ```sql
 *   SELECT documentdb_api.rename_collection('hr_database', 'employees', 'staff', true);
 *   -- Returns:
 *   --
 *   ```
 *
 * - Attempt to rename a collection to a name that already exists without dropping:
 *   ```sql
 *   SELECT documentdb_api.rename_collection('hr_database', 'employees', 'staff');
 *   -- Returns:
 *   -- ERROR:  collection "staff" already exists
 *   ```
 *

 * @param[in] p_database_name Name of the target database containing the collection to rename. Must not be NULL.
 * @param[in] p_collection_name Current name of the collection to rename. Must not be NULL.
 * @param[in] p_target_name New name for the collection. Must not be NULL and must adhere to naming conventions.
 * @param[in] p_drop_target (Optional) Boolean flag indicating whether to drop the target collection if it exists. Defaults to false.
 *
 * **Returns:**
 * - Void if the collection is renamed successfully.
 * - An error if the operation fails.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.rename_collection(
    p_database_name text,
    p_collection_name text,
    p_target_name text,
    p_drop_target bool default false)
RETURNS void
LANGUAGE c
VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $function$command_rename_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.rename_collection(text, text, text, bool)
    IS 'rename a collection';