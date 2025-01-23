
/*
 * __API_SCHEMA_V2__.find_and_modify processes a Mongo wire protocol findAndModify command.
 */
/**
 * @ingroup commands_crud
 * @brief Performs a find and modify operation on a DocumentDB collection.
 *
 * @details Executes a MongoDB find and modify command on a specified collection within the DocumentDB database. This function can find a single document based on a query and modify it according to the provided update parameters. It supports various options such as upserting documents, returning the document before or after modification, and participating in transactions.
 *
 * **Usage Examples:**
 * - Find a document and update a field:
 *   ```sql
 *   SELECT documentdb_api.find_and_modify('db', '{"findAndModify":"mycollection", "query":{"_id":1}, "update":{"$set":{"field":"value"}}, "new":true}');
 *   ```
 * - Find a document and remove it:
 *   ```sql
 *   SELECT documentdb_api.find_and_modify('db', '{"findAndModify":"mycollection", "query":{"_id":1}, "remove":true}');
 *   ```
 * - Upsert a document if it doesn't exist:
 *   ```sql
 *   SELECT documentdb_api.find_and_modify('db', '{"findAndModify":"mycollection", "query":{"_id":2}, "update":{"$set":{"field":"value"}}, "upsert":true, "new":true}');
 *   ```
 *
 * @param[in] p_database_name The name of the database where the find and modify operation will be executed. Must not be NULL.
 * @param[in] p_message A BSON document representing the find and modify command, which includes:
 *   - `"findAndModify"`: The name of the collection to operate on.
 *   - `"query"`: A BSON document specifying the selection criteria for the document.
 *   - `"update"`: A BSON document specifying the modifications to apply.
 *   - `"remove"`: A boolean indicating whether to remove the matching document (`true`) or not (`false`).
 *   - `"new"`: A boolean indicating whether to return the modified document (`true`) or the original document (`false`).
 *   - `"upsert"`: A boolean indicating whether to insert the document if it does not exist (`true`).
 * @param[in] p_transaction_id An optional transaction ID as a text string. If provided, the operation will be part of the specified transaction, supporting idempotent retries.
 * @param[out] p_result An OUT parameter that will contain a BSON document with the result of the operation, including the document before or after modification based on the `"new"` parameter.
 * @param[out] p_success An OUT boolean parameter that indicates whether the find and modify operation was successful (`true`) or if an error occurred (`false`).
 *
 * @return A record containing the operation result (`p_result`) and success flag (`p_success`).
 *
 * **Notes:**
 * - **Atomicity:** The find and modify operation is atomic and affects a single document.
 * - **Return Values:** The value of the `"new"` parameter determines whether the returned document is the original or modified version.
 * - **Shard Key Considerations:** In sharded environments, specifying the shard key in the query is important for efficiently locating the document.
 * - **Transactions:** If `p_transaction_id` is provided, the operation supports retryable writes within transactions.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.find_and_modify(
    p_database_name text,
    p_message __CORE_SCHEMA_V2__.bson,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA_V2__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_find_and_modify$$;
