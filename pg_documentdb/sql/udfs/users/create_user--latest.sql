/*
 * __API_SCHEMA_V2__.create_user processes a Mongo wire protocol createUser command.
 */
/**
 * @ingroup users
 * @brief Creates a new user in the DocumentDB system.
 *
 * @details This function creates a user based on the provided BSON specification. The
 *          specification should include all necessary details for user creation, such as 
 *          username, roles, and permissions.
 *
 * **Usage Examples:**
 * - Create a read-only user:
 *   ```sql
 *   SELECT documentdb_api.create_user('{"createUser":"test_user", "roles":[{"role":"readAnyDatabase","db":"admin"}]}');
 *   -- Returns:
 *   -- {
 *   --   "ok" : { "$numberInt" : "1" }
 *   -- }
 *   ```
 *
 * - Verify that the user is created:
 *   ```sql
 *   SELECT documentdb_api.users_info('{"usersInfo":"test_user"}');
 *   -- Returns:
 *   -- { "users" : [ { "_id" : "admin.test_user", "userId" : "admin.test_user", 
 *   -- "user" : "test_user", "db" : "admin", "roles" : [ { "role" : "readAnyDatabase", "db": "admin" } ] } ], "ok" : { "$numberInt" : "1" } }
 *   ```
 *
 * - Create an admin user:
 *   ```sql
 *   SELECT documentdb_api.create_user('{"createUser":"test_user2", "pwd":"", "roles":[{"role":"readWriteAnyDatabase","db":"admin"}, {"role":"clusterAdmin","db":"admin"}]}');
 *   -- Returns:
 *   -- {
 *   --   "ok" : { "$numberInt" : "1" }
 *   -- }
 *   ```

 * @param[in] p_spec BSON object containing the user creation specification. Must include "createUser", "pwd", and "roles" fields.
 *
 * **Returns:**
 * A BSON object representing the result of the user creation operation, including fields such as:
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.create_user(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_create_user$$);
