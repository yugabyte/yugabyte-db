-- collMod database command implementation for the Mongo wire protocol
/**
 * @ingroup schema_mgmt
 * @brief Modifies collection options in the DocumentDB system.
 *
 * @details The `coll_mod` function allows for the modification of collection settings within a specified database. It accepts a BSON-formatted specification detailing the changes to be applied, such as altering index settings or enabling TTL (Time-To-Live) for documents.
 *
 * **Usage Examples:**
 * - Modify an existing index to update `expireAfterSeconds`:
 *   ```sql
 *   SELECT documentdb_api.coll_mod('commands', 'collModTest', '{"collMod": "collModTest", "index": {"name": "a_1", "expireAfterSeconds": 2000}}');
 *   -- Returns:
 *   --  { "ok" : { "$numberInt" : "1" } }
 *   ```
 *
 * - Attempt to modify a non-TTL index:
 *   ```sql
 *   SELECT documentdb_api.coll_mod('commands', 'collModTest', '{"collMod": "collModTest", "index": {"name": "a_1"}}');
 *   -- Returns:
 *   -- ERROR:  no expireAfterSeconds or hidden field
 *   ```
 *
 * - Attempt to modify with an invalid field:
 *   ```sql
 *   SELECT documentdb_api.coll_mod('commands', 'collModTest', '{"collMod": "collModTest", "hello": 1}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'collMod.hello' is an unknown field.
 *   ```
 *
 *
 * @param[in] p_database_name Name of the target database. Must not be NULL.
 * @param[in] p_collection_name Name of the collection to modify. Must not be NULL.
 * @param[in] p_spec BSON object containing the modification specification. Must include the "collMod" field and relevant modification details.
 *
 * @return A BSON object representing the result of the collection modification operation, including fields such as:
 * - `ok`: Indicates the success status of the operation.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.coll_mod(
    IN p_database_name text,
    IN p_collection_name text, 
    IN p_spec __CORE_SCHEMA_V2__.bson)
RETURNS __CORE_SCHEMA_V2__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE
AS 'MODULE_PATHNAME', $function$command_coll_mod$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.coll_mod(text, text, __CORE_SCHEMA_V2__.bson)
    IS 'Updates the specification of collection';

