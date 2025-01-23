
/* SQL API function that creates a collection given a database and collection */
/**
 * @ingroup schema_mgmt
 * @brief Creates a new collection in the DocumentDB database.
 *
 * @details This function creates a collection in the specified database with the given name.
 *          The collection will be initialized according to the DocumentDB API specifications.
 *
 * **Usage Examples:**
 * - Create a new collection named "employees" in the "hr_database":
 *   ```sql
 *   SELECT documentdb_api.create_collection('hr_database', 'employees');
 *   -- Returns:
 *   -- true
 *   ```
 *
 * - Attempt to create a collection that already exists:
 *   ```sql
 *   SELECT documentdb_api.create_collection('hr_database', 'employees');
 *   -- Returns:
 *   -- false
 *   ```
 *

 * @param[in] p_database_name Name of the target database where the collection will be created. Must not be NULL.
 * @param[in] p_collection_name Name of the collection to create. Must not be NULL and must adhere to naming conventions.
 *
 * **Returns:**
 * - `true` if the collection is created successfully.
 * - `false` if the operation fails.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_collection(p_database_name text, p_collection_name text)
RETURNS bool
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_create_collection_core$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.create_collection(p_database_name text, p_collection_name text)
    IS 'create a collection';
