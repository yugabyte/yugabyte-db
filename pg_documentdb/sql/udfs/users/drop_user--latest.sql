/*
 * __API_SCHEMA_V2__.drop_user processes a Mongo wire protocol dropUser command.
 */
/**
 * @ingroup users
 * @brief Drops an existing user in the DocumentDB system.
 *
 * @details This function removes a user based on the provided BSON specification. The
 *          specification should include sufficient details to identify the user to be dropped,
 *          such as the username and the associated database.
 *
 * **Usage Examples:**
 * - Drop an existing user:
 *   ```sql
 *   SELECT documentdb_api.drop_user('{"dropUser":"test_user"}');
 *   -- Returns:
 *   -- {
 *   --   "ok" : { "$numberInt" : "1" }
 *   -- }
 *   ```
 *
 * - Attempt to drop a non-existent user:
 *   ```sql
 *   SELECT documentdb_api.drop_user('{"dropUser":"nonexistent_user"}');
 *   -- Returns:
 *   -- ERROR:  role "nonexistent_user" does not exist
 *   ```
 * @param[in] p_spec BSON object containing the user drop specification. Must include the "dropUser" field specifying the username.
 *
 * @return A BSON object representing the result of the user drop operation, including fields such as:
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.drop_user(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_drop_user$$);
