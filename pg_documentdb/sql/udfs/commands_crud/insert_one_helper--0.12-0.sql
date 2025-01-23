
/*
 * __API_SCHEMA_V2__.insert_one is a convenience function for inserting a single
 * single document.
 */
/**
 * @brief Inserts a single document into a collection.
 * @ingroup commands_crud
 *
 * @details This function inserts a single BSON document into the specified collection 
 *          within the given database. It utilizes the underlying `documentdb_api.insert` 
 *          function to perform the insert operation. The function can optionally include 
 *          a transaction ID if the insert is part of a larger transaction.
 *
 * **Usage Examples:**
 * - Successful single-row insert:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"_id":99,"a":99}');
 *   -- Returns:
 *   -- {
 *   --   "n": { "$numberInt": "1" },
 *   --   "ok": { "$numberDouble": "1.0" }
 *   -- }
 *   ```
 *
 * - Attempt to insert with NULL document:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', NULL);
 *   -- Returns:
 *   -- ERROR:  insert document cannot be NULL
 *   ```
 *
 * - Insert with missing required fields:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"documents":[{"a":1}]}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.insert' is missing but a required field
 *   ```
 *
 * - Insert into a non-existent database:
 *   ```sql
 *   SELECT documentdb_api.insert_one(NULL, 'into', '{"a":1}');
 *   -- Returns:
 *   -- ERROR:  database name cannot be NULL
 *   ```
 *
 * - Insert with invalid collection name type:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', '["into"]', '{"a":1}');
 *   -- Returns:
 *   -- ERROR:  collection name has invalid type array
 *   ```
 *
 * - Insert with invalid documents field type:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"documents": {"a":1}}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.documents' is the wrong type 'object', expected type 'array'
 *   ```
 *
 * - Insert with unknown fields:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"insert":"into", "documents":[{"a":1}], "extra":1}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.extra' is an unknown field
 *   ```
 *
 * - Insert with invalid document type in documents array:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"insert":"into", "documents":[4]}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.documents.0' is the wrong type 'int', expected type 'object'
 *   ```
 *
 * - Insert with invalid `ordered` field type:
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'into', '{"insert":"into", "documents":[{"a":1}], "ordered":1}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.ordered' is the wrong type 'int', expected type 'bool'
 *   ```
 *
 * - Insert into system collections (not allowed):
 *   ```sql
 *   SELECT documentdb_api.insert_one('db', 'system.views', '{"a":1}');
 *   -- Returns:
 *   -- ERROR:  cannot write to db.system.views
 *   ```
 *

 * @param[in] p_database_name The name of the database where the document will be inserted. Must not be NULL.
 * @param[in] p_collection_name The name of the collection within the database where the document will be inserted. Must not be NULL.
 * @param[in] p_document The BSON document to be inserted into the collection. Must not be NULL.
 * @param[in] p_transaction_id The transaction ID, if the insert operation is part of a transaction (optional). Defaults to NULL.
 *
 * @return A BSON object representing the result of the insert operation, which includes:
 * - `"n"`: The number of documents inserted.
 * - `"ok"`: Indicates whether the operation was successful (`1.0` for success, `0.0` for failure).
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.insert_one(
    p_database_name text,
    p_collection_name text,
    p_document __CORE_SCHEMA_V2__.bson,
    p_transaction_id text default null)
RETURNS __CORE_SCHEMA_V2__.bson
SET search_path TO __CORE_SCHEMA_V2__, pg_catalog
AS $fn$
DECLARE
    v_insert __CORE_SCHEMA_V2__.bson;
    p_resultBson __CORE_SCHEMA_V2__.bson;
BEGIN
	v_insert := ('{"insert":"' || p_collection_name || '" }')::bson;

    SELECT p_result INTO p_resultBson FROM __API_SCHEMA_V2__.insert(
      p_database_name,
      v_insert,
      p_document::bsonsequence,
      p_transaction_id);
    RETURN p_resultBson;
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION __API_SCHEMA_V2__.insert_one(text,text,__CORE_SCHEMA_V2__.bson,text)
    IS 'insert one document into a collection';
