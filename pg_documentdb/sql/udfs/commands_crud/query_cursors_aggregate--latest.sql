
/**
 * @ingroup commands_crud
 * @brief Executes an aggregation command and retrieves the first page of results from the cursor.
 *
 * @details This function initiates an aggregation operation on the specified database and collection.
 *          It processes the provided BSON-formatted aggregation pipeline and returns the first batch
 *          of results. An optional cursor ID can be supplied to continue an existing aggregation session.
 *
 * **Usage Examples (referenced from BSON_AGGREGATION_STAGE_MERGE_TESTS.OUT):**
 * - Aggregate with a `$merge` stage while creating a new target collection:
 *   ```sql
 *   SELECT * FROM aggregate_cursor_first_page('defDb', '{ "aggregate": "defSrc", "pipeline": [  {"$merge" : { "into": "defTar" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
 *   -- NOTICE:  creating collection
 *   -- Returns an empty "firstBatch" if the pipeline finishes quickly,
 *   -- with "ok" : { "$numberDouble" : "1.0" }.
 *   ```
 *
 * - `$merge` into a collection in a different database:
 *   ```sql
 *   SELECT * FROM aggregate_cursor_first_page(
 *       'sourceDB',
 *       '{ "aggregate": "sourceDumpData", "pipeline": [ 
 *           {"$merge" : { "into": { "db" : "targetDB", "coll" : "targetDumpData" }, "on" : "_id", "whenMatched" : "replace" }} 
 *         ], "cursor": { "batchSize": 1 } }',
 *       4294967294
 *   );
 *   -- NOTICE:  creating collection
 *   -- Returns an empty "firstBatch" and updates the target collection in "targetDB".
 *   ```
 *
 * - Aggregate a collection after inserting documents:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db','salaries',' { "_id" :  6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }', NULL);
 *   SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "salaries", "pipeline": [  {"$merge" : { "into": "budgets" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
 *   -- Creates "budgets" if it doesn't exist and merges data accordingly.
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] commandSpec BSON object containing the aggregation command specification. Must include the "aggregate" collection and a valid "pipeline".
 * @param[in] cursorId (Optional) ID of an existing cursor. Defaults to 0, indicating a new aggregation session.
 * @param[out] cursorPage BSON object containing the first page of aggregation results.
 * @param[out] continuation BSON object with metadata for continuing the cursor, such as `cursor_id` and `batch_size`.
 * @param[out] persistConnection Boolean indicating whether to keep the connection open for further cursor operations.
 * @param[out] cursorId ID of the active cursor after executing the aggregation.
 *
 * @return A record containing the following fields:
 * - `cursorPage`: BSON object with the documents retrieved in the first page of the aggregation cursor.
 * - `continuation`: BSON object containing metadata for continuing the cursor.
 * - `persistConnection`: Boolean indicating if the connection should remain open.
 * - `cursorId`: The unique identifier of the active cursor.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.aggregate_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_aggregate_cursor_first_page$function$;

/**
 * @ingroup commands_crud
 * @brief Executes a find command and retrieves the first page of results from the cursor.
 *
 * @details This function performs a find operation on the specified database and collection.
 *          It processes the provided BSON-formatted query and returns the first batch of matching documents.
 *          An optional cursor ID can be supplied to continue an existing find session.
 *
 * **Usage Examples:**
 * - Retrieve documents by simple filter:
 *   ```sql
 *   SELECT documentdb_api.find_cursor_first_page(
 *       'my_database',
 *       '{"find": "my_collection", "filter": { "type": "example" }}'
 *   );
 *   -- Returns the first page with matching documents and a cursor ID for continuation.
 *   ```
 *
 * - Continue an existing find operation:
 *   ```sql
 *   SELECT documentdb_api.find_cursor_first_page(
 *       'my_database',
 *       '{"find": "my_collection", "filter": { "type": "example" }}'::documentdb_core.bson,
 *       67890
 *   );
 *   -- Returns the next batch of documents from an existing cursor session.
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] commandSpec BSON object containing the find command specification. Must include the "find" collection and a valid "filter".
 * @param[in] cursorId (Optional) ID of an existing cursor. Defaults to 0, indicating a new find session.
 * @param[out] cursorPage BSON object containing the first page of find results.
 * @param[out] continuation BSON object with metadata for continuing the cursor, such as `cursor_id` and `batch_size`.
 * @param[out] persistConnection Boolean indicating whether to keep the connection open for further cursor operations.
 * @param[out] cursorId ID of the active cursor after executing the find command.
 *
 * @returns A record containing the following fields:
 * - `cursorPage`: BSON object with the documents retrieved in the first page of the find cursor.
 * - `continuation`: BSON object containing metadata for continuing the cursor.
 * - `persistConnection`: Boolean indicating if the connection should remain open.
 * - `cursorId`: The unique identifier of the active cursor.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.find_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_find_cursor_first_page$function$;

/**
 * @ingroup commands_crud
 * @brief Lists collections in a database and retrieves the first page of results from the cursor.
 *
 * @details This function retrieves the list of collections within the specified database.
 *          It processes the provided BSON-formatted command and returns the first batch of collection information.
 *          An optional cursor ID can be supplied to continue an existing list collections session.
 *
 * **Usage Examples:**
 * - List collections and retrieve the first page of results:
 *   ```sql
 *   SELECT documentdb_api.list_collections_cursor_first_page(
 *       'my_database',
 *       '{"listCollections": 1, "filter": { "type": "collection" }}');
 *   -- Returns:
 *   -- {
 *   --   "cursorPage": { "collections": [ ... ] },
 *   --   "continuation": { "cursor_id": 11223, "batch_size": 20 },
 *   --   "persistConnection": true,
 *   --   "cursorId": 11223
 *   -- }
 *   ```
 *
 * - Continue listing collections with an existing cursor ID:
 *   ```sql
 *   SELECT documentdb_api.list_collections_cursor_first_page(
 *       'my_database',
 *       '{"listCollections": 1, "filter": { "type": "collection" }}'::documentdb_core.bson,
 *       11223);
 *   -- Returns:
 *   -- {
 *   --   "cursorPage": { "collections": [ ... ] },
 *   --   "continuation": { "cursor_id": 11223, "batch_size": 20 },
 *   --   "persistConnection": true,
 *   --   "cursorId": 11223
 *   -- }
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] commandSpec BSON object containing the listCollections command specification. Must include the "listCollections" command and a valid "filter".
 * @param[in] cursorId (Optional) ID of an existing cursor. Defaults to 0, indicating a new list collections session.
 * @param[out] cursorPage BSON object containing the first page of listCollections results.
 * @param[out] continuation BSON object with metadata for continuing the cursor, such as `cursor_id` and `batch_size`.
 * @param[out] persistConnection Boolean indicating whether to keep the connection open for further cursor operations.
 * @param[out] cursorId ID of the active cursor after executing the listCollections command.
 *
 * **Returns:**
 * A record containing the following fields:
 * - `cursorPage`: BSON object with the collections retrieved in the first page of the cursor.
 * - `continuation`: BSON object containing metadata for continuing the cursor.
 * - `persistConnection`: Boolean indicating if the connection should remain open.
 * - `cursorId`: The unique identifier of the active cursor.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.list_collections_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_collections_cursor_first_page$function$;

/**
 * @ingroup commands_crud
 * @brief Lists collections in a database and retrieves the first page of results from the cursor.
 *
 * @details This function retrieves the list of collections within the specified database.
 *          It processes the provided BSON-formatted command and returns the first batch of collection information.
 *          An optional cursor ID can be supplied to continue an existing listCollections session.
 *
 * **Usage Examples:**
 * - List collections with a specific filter:
 *   ```sql
 *   SELECT documentdb_api.list_collections_cursor_first_page(
 *       'my_database',
 *       '{"listCollections": 1, "filter": { "type": "collection" }}'
 *   );
 *   -- Returns a cursorPage containing collection metadata.
 *   ```
 *
 * - Continue listing collections:
 *   ```sql
 *   SELECT documentdb_api.list_collections_cursor_first_page(
 *       'my_database',
 *       '{"listCollections": 1, "filter": { "type": "collection" }}'::documentdb_core.bson,
 *       11223
 *   );
 *   -- Continues from an existing cursor session.
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] commandSpec BSON object containing the listCollections command specification. Must include "listCollections" and a valid "filter".
 * @param[in] cursorId (Optional) ID of an existing cursor. Defaults to 0, indicating a new listCollections session.
 * @param[out] cursorPage BSON object containing the first page of listCollections results.
 * @param[out] continuation BSON object with metadata for continuing the cursor, such as `cursor_id` and `batch_size`.
 * @param[out] persistConnection Boolean indicating whether to keep the connection open for further cursor operations.
 * @param[out] cursorId ID of the active cursor after executing the listCollections command.
 *
 * **Returns:**
 * A record containing the following fields:
 * - `cursorPage`: BSON object with the collections retrieved in the first page of the cursor.
 * - `continuation`: BSON object containing metadata for continuing the cursor.
 * - `persistConnection`: Boolean indicating if the connection should remain open.
 * - `cursorId`: The unique identifier of the active cursor.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.list_indexes_cursor_first_page(
    database text, commandSpec __CORE_SCHEMA_V2__.bson, cursorId int8 DEFAULT 0, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson, OUT persistConnection bool, OUT cursorId int8)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_list_indexes_cursor_first_page$function$;

/**
 * @ingroup commands_crud
 * @brief Lists indexes in a collection and retrieves the first page of results from the cursor.
 *
 * @details This function retrieves the list of indexes for a specified collection within the given database.
 *          It processes the provided BSON-formatted command and returns the first batch of index information.
 *          An optional cursor ID can be supplied to continue an existing listIndexes session.
 *
 * **Usage Examples:**
 * - List indexes for a collection:
 *   ```sql
 *   SELECT documentdb_api.list_indexes_cursor_first_page(
 *       'my_database',
 *       '{"listIndexes": "my_collection"}'
 *   );
 *   -- Returns the first batch of index metadata and a cursor ID for continuation.
 *   ```
 *
 * - Continue listing indexes:
 *   ```sql
 *   SELECT documentdb_api.list_indexes_cursor_first_page(
 *       'my_database',
 *       '{"listIndexes": "my_collection"}'::documentdb_core.bson,
 *       44556
 *   );
 *   -- Resumes at an existing cursor session for index listing.
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] commandSpec BSON object containing the listIndexes command specification. Must include the "listIndexes" collection.
 * @param[in] cursorId (Optional) ID of an existing cursor. Defaults to 0, indicating a new listIndexes session.
 * @param[out] cursorPage BSON object containing the first page of listIndexes results.
 * @param[out] continuation BSON object with metadata for continuing the cursor, such as `cursor_id` and `batch_size`.
 * @param[out] persistConnection Boolean indicating whether to keep the connection open for further cursor operations.
 * @param[out] cursorId ID of the active cursor after executing the listIndexes command.
 *
 * @return A record containing the following fields:
 * - `cursorPage`: BSON object with the indexes retrieved in the first page of the cursor.
 * - `continuation`: BSON object containing metadata for continuing the cursor.
 * - `persistConnection`: Boolean indicating if the connection should remain open.
 * - `cursorId`: The unique identifier of the active cursor.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.cursor_get_more(
    database text, getMoreSpec __CORE_SCHEMA_V2__.bson, continuationSpec __CORE_SCHEMA_V2__.bson, OUT cursorPage __CORE_SCHEMA_V2__.bson, OUT continuation __CORE_SCHEMA_V2__.bson)
RETURNS record
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_cursor_get_more$function$;