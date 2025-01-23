/**
 * @ingroup index_mgmt
 * @brief Drops index(es) from a MongoDB collection.
 *
 * @details This procedure removes one or more indexes from a specified collection in a MongoDB database. It processes the `dropIndexes` command, which can drop a single index by name, multiple indexes, or all indexes on a collection except for the default index on the `_id` field. The operation's results are returned in the `retval` parameter.
 *
 * **Usage Examples:**
 * - Drop a single index by name:
 *   ```sql
 *   documentdb_api.drop_indexes('db', '{"dropIndexes": "collection_1", "index": "index_name"}');
 *   ```
 * - Drop multiple indexes by names:
 *   ```sql
 *   documentdb_api.drop_indexes('db', '{"dropIndexes": "collection_1", "index": ["index1", "index2"]}');
 *   ```
 *

 * @param[in] p_database_name The name of the database containing the target collection. Must not be NULL.
 * @param[in] p_arg A BSON document specifying the indexes to be dropped. It must include:
 *   - `"dropIndexes"`: The name of the collection from which to drop indexes.
 *   - `"index"`: The name of the index to drop, an array of index names, or `"*"` to drop all user-defined indexes.
 * @param[in,out] retval A BSON document that will be updated with the result of the operation, including information such as the number of indexes dropped and any error messages. Defaults to NULL if not provided.
 *
 * @return The `retval` parameter will contain a BSON document with the result of the operation, which includes fields like:
 * - `"ok"`: Indicates whether the operation was successful (`true`) or not (`false`).
 * - `"nIndexesWas"`: The number of indexes before the drop operation.
 * **Notes:**
 * - Dropping indexes is a blocking operation and may impact performance.
 * - When dropping multiple indexes, if any index does not exist, the entire operation will fail.
 * - Use caution when dropping indexes, as it may affect query performance.
 * - The function does not allow dropping the default `_id` index.
 */
CREATE OR REPLACE PROCEDURE __API_SCHEMA__.drop_indexes(IN p_database_name text, IN p_arg __CORE_SCHEMA__.bson,
                                                      INOUT retval __CORE_SCHEMA__.bson DEFAULT null)
 LANGUAGE c
AS 'MODULE_PATHNAME', $procedure$command_drop_indexes$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA__.drop_indexes(text, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
    IS 'drop index(es) from a collection';