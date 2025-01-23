

/*
 * __API_SCHEMA_V2__.create_collection_view processes a Mongo wire protocol create command.
 */
/**
 * @ingroup schema_mgmt
 * @brief Creates a new collection view in the DocumentDB database.
 *
 * @details The `create_collection_view` function establishes a collection view within the specified database using the provided BSON specification. The specification outlines the view's structure, configuration, and any associated query or transformation logic, enabling customized data representations without duplicating data.
 *
 * **Usage Examples:**
 * - Create a simple collection view:
 *   ```sql
 *   SELECT documentdb_api.create_collection_view(
 *       'my_database',
 *       '{ "create": "my_view", "viewOn": "my_collection", "pipeline": [{ "$match": { "status": "active" } }]}');
 *   -- Returns:
 *   -- {
 *   --   "ok" : { "$numberDouble" : "1.0" }
 *   -- }
 *   ```
 *
 * - Create a collection view with aggregation stages:
 *   ```sql
 *   SELECT documentdb_api.create_collection_view(
 *       'my_database',
 *       '{ "create": "active_users_view", "viewOn": "users", "pipeline": [{ "$match": { "active": true } }, { "$project": { "username": 1, "email": 1 } }]}');
 *   -- Returns:
 *   -- {
 *   --   "ok" : { "$numberDouble" : "1.0" }
 *   -- }
 *   ```
 *
 * - Handle errors when creating a collection view:
 *   - Creating a view that already exists:
 *     ```sql
 *     SELECT documentdb_api.create_collection_view(
 *         'my_database',
 *         '{ "create": "my_view", "viewOn": "my_collection", "pipeline": [{ "$match": { "status": "active" } }]}');
 *     -- Returns:
 *     -- ERROR:  collection view "my_view" already exists
 *     ```
 *   - Providing an invalid specification:
 *     ```sql
 *     SELECT documentdb_api.create_collection_view(
 *         'my_database',
 *         '{ "create": "", "viewOn": "my_collection"}');
 *     -- Returns:
 *     -- ERROR:  Invalid namespace specified 'my_database.'
 *     ```
 *

 * @param[in] dbname Name of the target database where the collection view will be created. Must not be NULL.
 * @param[in] createSpec BSON object specifying the details for the new collection view. Must include fields such as "create", "viewOn", and "pipeline".
 *
 * @return A BSON object representing the result of the view creation operation, including fields such as:
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_collection_view(dbname text, createSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $$command_create_collection_view$$;