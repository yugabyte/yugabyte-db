
/*
 * __API_SCHEMA__.collection() can be used to query collections, e.g.:
 * SELECT * FROM __API_SCHEMA__.collection('db','collection')
 *
 * While this seems slow, we use the planner hook to replace this function
 * directly with the table, so we usually do not call it directly.
 *
 * Output arguments need to match data tables exactly.
 */
/**
 * @ingroup metadata
 * @brief Retrieves metadata and documents from a specified Mongo collection.
 *
 * @details This function queries a MongoDB collection within the specified database, retrieving both metadata (such as shard key and object ID) and document content along with their creation times.
 *
 * **Usage Examples:**
 * - Retrieve metadata and documents from a collection:
 *   ```sql
 *   SELECT count(*) from documentdb_api.collection(
 *       'my_database',
 *       'my_collection');
 *   -- Returns:
 *   -- count 
 *   -- -------
 *   --   9
 *   ```
 *
 * - Query a non-existent collection:
 *   ```sql
 *   SELECT count(*) from documentdb_api.collection(
 *       'my_database',
 *       'my_collection');
 *   -- Returns:
 *   -- count 
 *   -- -------
 *   --   0
 *   ```
 *

 * @param[in] p_database_name Name of the target database. Must not be NULL.
 * @param[in] p_collection_name Name of the collection to query. Must not be NULL.
 * @param[out] shard_key_value Shard key value associated with the collection.
 * @param[out] object_id BSON object representing the object ID of the document.
 * @param[out] document BSON object containing the document content.
 * @param[out] creation_time Timestamp of the document's creation time.
 *
 * @return A set of records with metadata and document information.
 * @note collection function should be only used in a FROM clause
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.collection(
    p_database_name text,
    p_collection_name text,
    OUT shard_key_value bigint,
    OUT object_id __CORE_SCHEMA__.bson,
    OUT document __CORE_SCHEMA__.bson,
    OUT creation_time timestamptz)
RETURNS SETOF record
LANGUAGE c
 STRICT
AS 'MODULE_PATHNAME', $function$command_api_collection$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.collection(text,text)
    IS 'query a collection';