
/*
 * __API_SCHEMA_V2__.delete processes a Mongo wire protocol delete command.
 */
/**
 * @ingroup commands_crud
 * @brief Deletes documents from a DocumentDB collection.
 *
 * @details Processes a MongoDB wire protocol delete command to remove documents from a specified collection in the DocumentDB database. This function handles various delete operations including deleting all documents, deleting with specific filters, handling ordered and unordered deletes, and managing transactions. It also provides detailed error messages for invalid inputs or query syntax errors as observed in the test outputs.
 *
 * **Usage Examples:**
 * - Delete all documents from a collection:
 *   ```sql
 *   SELECT documentdb_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{},"limit":0}]}');
 *   ```
 * - Delete documents with a filter:
 *   ```sql
 *   SELECT documentdb_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"a":{"$lte":3}},"limit":0}]}');
 *   ```
 * - Delete a single document using `_id`:
 *   ```sql
 *   SELECT documentdb_api.delete('db', '{"delete":"removeme", "deletes":[{"q":{"_id":6},"limit":1}]}');
 *   ```
 * @param[in] p_database_name The name of the database containing the target collection from which documents will be deleted. Must not be NULL.
 * @param[in] p_delete A BSON document representing the delete command, which includes the collection name (`"delete":"collection_name"`) and an array of delete specifications (`"deletes":[]`). Each delete specification must contain:
 *   - `"q"`: A BSON document specifying the query criteria to match the documents that should be deleted.
 *   - `"limit"`: An integer indicating the number of matched documents to delete. Use `0` for all matching documents or `1` for a single document.
 * @param[in] p_insert_documents An optional BSON sequence of documents to insert after the deletion operation. If no documents are to be inserted, this parameter can be set to NULL.
 * @param[in] p_transaction_id An optional transaction ID as a text string. If provided, the delete operation will be part of the specified transaction. Supports idempotent retries of write operations.
 * @param[out] p_result An OUT parameter that will contain a BSON document detailing the results of the delete operation, such as the number of documents deleted (`"n"`), and any write errors.
 * @param[out] p_success An OUT boolean parameter that indicates whether the delete operation was successful (`true`) or if an error occurred (`false`).
 *
 * @returns A record containing the delete operation result (`p_result`) and success flag (`p_success`).
 *
 * **Notes:**
 * - **Ordered vs Unordered Deletes:** The `"ordered"` field in the delete command determines if the operation should stop at the first error (`true`) or continue processing remaining deletes (`false`).
 * - **Transactions:** If `p_transaction_id` is provided, the delete operation supports retryable writes within transactions.
 * - **Limitations:**
 *   - Deleting without filters (`"q":{}`) and `limit` set to `1` or `0` behaves respectively as deleting a single document or all documents.
 *   - Deleting from non-existent collections returns a successful result with `"n" : 0`.
 * - **Shard Key Considerations:** In sharded environments, specifying the shard key is important for efficient deletions.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.delete(
    p_database_name text,
    p_delete __CORE_SCHEMA_V2__.bson,
    p_insert_documents __CORE_SCHEMA_V2__.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA_V2__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_delete$$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.delete(text,__CORE_SCHEMA_V2__.bson,__CORE_SCHEMA_V2__.bsonsequence,text)
    IS 'deletes documents from a collection';

/* Command: delete */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.delete_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_query __CORE_SCHEMA_V2__.bson,
    p_sort __CORE_SCHEMA_V2__.bson,
    p_return_document bool,
    p_return_fields __CORE_SCHEMA_V2__.bson,
    p_transaction_id text,
    OUT o_is_row_deleted bool,
    OUT o_result_deleted_document __CORE_SCHEMA_V2__.bson)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_delete_one$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.delete_one(bigint,bigint,__CORE_SCHEMA_V2__.bson,__CORE_SCHEMA_V2__.bson,bool,__CORE_SCHEMA_V2__.bson,text)
    IS 'deletes a single document from a collection';


/* Command: delete */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.delete_worker(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_shard_oid regclass,
    p_update_internal_spec __CORE_SCHEMA_V2__.bson,
    p_update_internal_docs __CORE_SCHEMA_V2__.bsonsequence,
    p_transaction_id text)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_delete_worker$$;