/*
 * __API_SCHEMA_V2__.users_info processes a Mongo wire protocol usersInfo command.
 */
/**
 * @ingroup users
 * @brief Retrieves information about users in the DocumentDB system.
 *
 * @details This function fetches user information based on the provided BSON specification. 
 *          The specification can include various filters or criteria to narrow down the results, such as
 *          specific usernames, roles, or associated databases.
 *
 * **Usage Examples:**
 * - Retrieve information for a specific user:
 *   ```sql
 *   SELECT documentdb_api.users_info('{"usersInfo":"test_user"}');
 *   -- Returns:
 *   -- {
 *   --   "users" : [ 
 *   --     { 
 *   --       "_id" : "admin.test_user", 
 *   --       "userId" : "admin.test_user", 
 *   --       "user" : "test_user", 
 *   --       "db" : "admin", 
 *   --       "roles" : [ { "role" : "readAnyDatabase", "db" : "admin" } ] 
 *   --     } 
 *   --   ], 
 *   --   "ok" : { "$numberInt" : "1" } 
 *   -- }
 *   ```
 *
 * - Retrieve information for all users in all databases:
 *   ```sql
 *   SELECT documentdb_api.users_info('{"forAllDBs":true}');
 *   -- Returns:
 *   -- {
 *   --   "users" : [ 
 *   --     { "_id" : "admin.test_user", "userId" : "admin.test_user", "user" : "test_user", "db" : "admin", "roles" : [ { "role" : "readAnyDatabase", "db" : "admin" } ] },
 *   --     { "_id" : "admin.test_user2", "userId" : "admin.test_user2", "user" : "test_user2", "db" : "admin", "roles" : [ { "role" : "readWriteAnyDatabase", "db" : "admin" }, { "role" : "clusterAdmin", "db" : "admin" } ] },
 *   --     { "_id" : "admin.test_user4", "userId" : "admin.test_user4", "user" : "test_user4", "db" : "admin", "roles" : [ { "role" : "readWriteAnyDatabase", "db" : "admin" }, { "role" : "clusterAdmin", "db" : "admin" } ] },
 *   --     { "_id" : "admin.test_user5", "userId" : "admin.test_user5", "user" : "test_user5", "db" : "admin", "roles" : [ { "role" : "readWriteAnyDatabase", "db" : "admin" }, { "role" : "clusterAdmin", "db" : "admin" } ] }
 *   --   ], 
 *   --   "ok" : { "$numberInt" : "1" } 
 *   -- }
 *   ```
 *
 * @param[in] p_spec BSON object containing the query criteria for retrieving user information. This can include:
 *          - `"usersInfo"`: Specifies the username or an array of usernames to retrieve information for.
 *          - `"forAllDBs"`: If set to `true`, retrieves user information across all databases.
 *          - Additional filters or criteria as needed.
 *
 * **Returns:**
 * A BSON object containing information about the matched users, including fields such as:
 * - `users`: An array of user objects, each containing:
 *   - `_id`: The unique identifier of the user.
 *   - `userId`: The user ID.
 *   - `user`: The username.
 *   - `db`: The associated database.
 *   - `roles`: An array of roles assigned to the user.
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.users_info(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_get_users$$);
