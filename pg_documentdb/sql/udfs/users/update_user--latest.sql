/*
 * __API_SCHEMA_V2__.update_user processes a Mongo wire protocol updateUser command.
 */
/**
 * @ingroup users
 * @brief Updates an existing user in the DocumentDB system.
 *
 * @details This function modifies a user's details based on the provided BSON specification. 
 *          The specification should include the user identifier and the fields to update, 
 *          such as roles, permissions, or profile information.
 *
 * **Usage Examples:**
 * - Update a user:
 *   ```sql
 *   SELECT documentdb_api.update_user('{"updateUser":"test_user"}');
 *   -- Returns:
 *   -- { "ok" : { "$numberDouble" : "1.0" } }
 *   ```
 *

 * @param[in] p_spec BSON object containing the user update specification. Must include the "updateUser" field specifying the username and at least one field to update such as "roles", "pwd", or "customData".
 *
 * @return A BSON object representing the result of the user update operation, including fields such as:
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.update_user(
    p_spec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($$, __EXTENSION_OBJECT_PREFIX_V2__, _extension_update_user$$);
