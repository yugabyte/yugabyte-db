
/*
 * processes a Mongo insert wire protocol command.
 */
/**
 * @brief Inserts documents into a DocumentDB collection for a Mongo wire protocol command.
 * @ingroup commands_crud
 *
 * @details This function inserts one or more BSON documents into the specified collection 
 *          within the given database. It leverages the underlying `documentdb_api.insert` 
 *          function to perform the insert operation. The function can optionally include 
 *          a transaction ID if the insert is part of a larger transaction.
 *
 * **Usage Examples:**
 * - Successful single-row insert:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');
 *   -- Returns:
 *   -- ("{ ""n"" : { ""$numberInt"" : ""1"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *   ```
 *
 * - Attempt to insert with NULL document:
 *   ```sql
 *   SELECT documentdb_api.insert('db', NULL);
 *   -- Returns:
 *   -- ERROR:  insert document cannot be NULL
 *   ```
 *
 * - Insert with missing required fields:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"documents":[{"a":1}]}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.insert' is missing but a required field
 *   ```
 *
 * - Insert into a non-existent database:
 *   ```sql
 *   SELECT documentdb_api.insert(NULL, '{"insert":"into", "documents":[{"a":1}]}');
 *   -- Returns:
 *   -- ERROR:  database name cannot be NULL
 *   ```
 *
 * - Insert with invalid collection name type:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":["into"], "documents":[{"a":1}]}');
 *   -- Returns:
 *   -- ERROR:  collection name has invalid type array
 *   ```
 *
 * - Insert with invalid documents field type:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":{"a":1}}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.documents' is the wrong type 'object', expected type 'array'
 *   ```
 *
 * - Insert with unknown fields:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}], "extra":1}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.extra' is an unknown field
 *   ```
 *
 * - Insert with invalid document type in documents array:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[4]}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.documents.0' is the wrong type 'int', expected type 'object'
 *   ```
 *
 * - Insert with invalid `ordered` field type:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"a":1}], "ordered":1}');
 *   -- Returns:
 *   -- ERROR:  BSON field 'insert.ordered' is the wrong type 'int', expected type 'bool'
 *   ```
 *
 * - Insert into system collections (not allowed):
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"system.views", "documents":[{"a":1}], "ordered":true}');
 *   -- Returns:
 *   -- ERROR:  cannot write to db.system.views
 *   ```
 *
 * - Insert into a database with the same name but different case and the same collection:
 *   ```sql
 *   SELECT documentdb_api.insert('dB', '{"insert":"into", "documents":[{"_id":99,"a":99}]}');
 *   -- Returns:
 *   -- ERROR:  db already exists with different case already have: [db] trying to create [dB]
 *   ```
 *
 * - Insert into a database with the same name but different case and a different collection:
 *   ```sql
 *   SELECT documentdb_api.insert('dB', '{"insert":"intonew", "documents":[{"_id":99,"a":99}]}');
 *   -- Returns:
 *   -- ERROR:  db already exists with different case already have: [db] trying to create [dB]
 *   ```
 *
 * - Insert into the same database and a new collection:
 *   ```sql
 *   SELECT documentdb_api.insert('db', '{"insert":"intonew1", "documents":[{"_id":99,"a":99}]}');
 *   -- Returns:
 *   -- NOTICE:  creating collection
 *   -- insert
 *   -- ---------------------------------------------------------------------
 *   -- ("{ ""n"" : { ""$numberInt"" : ""1"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *   -- (1 row)
 *   ```
 *
 * - Insert with duplicate keys in a transaction:
 *   ```sql
 *   BEGIN;
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}', NULL, 'insert-1');
 *   -- Returns:
 *   -- ("{ ""n"" : { ""$numberInt"" : ""1"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1}]}', NULL, 'insert-1');
 *   -- Returns:
 *   -- ("{ ""n"" : { ""$numberInt"" : ""1"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *   -- (1 row)
 *
 *   SELECT document FROM documentdb_api.collection('db','into') WHERE document @@ '{}';
 *   -- Returns:
 *   -- { "_id" : { "$numberInt" : "1" }, "a" : { "$numberInt" : "1" } }
 *   -- (1 row)
 *
 *   ROLLBACK;
 *   ```
 *
 * - Insert with documents in a special section:
 *   ```sql
 *   BEGIN;
 *   SELECT documentdb_api.insert('db', '{"insert":"into"}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
 *   -- Returns:
 *   -- ("{ ""n"" : { ""$numberInt"" : ""2"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *
 *   SELECT document FROM documentdb_api.collection('db','into') WHERE document @@ '{}' ORDER BY document-> '_id';
 *   -- Returns:
 *   -- { "_id" : { "$numberInt" : "2" }, "a" : { "$numberInt" : "2" } }
 *   -- (2 rows)
 *   
 *   ROLLBACK;
 *   ```
 *
 * - Insert with both documents specified:
 *   ```sql
 *   BEGIN;
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":1,"a":1},{"_id":2,"a":2}]}', '{ "": [{"_id":1,"a":1},{"_id":2,"a":2}] }');
 *   -- Returns:
 *   -- ERROR:  Unexpected additional documents
 *   
 *   ROLLBACK;
 *   ```
 *
 * - Insert with `_id` undefined (skips the insert):
 *   ```sql
 *   BEGIN;
 *   SELECT documentdb_api.insert('db', '{"insert":"into", "documents":[{"_id":{ "$undefined": true } }]}');
 *   -- Returns:
 *   -- ("{ ""n"" : { ""$numberInt"" : ""0"" }, ""ok"" : { ""$numberDouble"" : ""1.0"" } }",t)
 *   
 *   ROLLBACK;
 *   ```
 *
 * @param[in] p_database_name The name of the database where the documents will be inserted. Must not be NULL.
 * @param[in] p_insert The BSON object representing the insert command. It must include:
 *                      - `"insert"`: The name of the collection where the document will be inserted.
 *                      - `"documents"`: An array of BSON documents to be inserted.
 * @param[in] p_insert_documents A BSON sequence of additional documents to be inserted (optional). Defaults to NULL.
 * @param[in] p_transaction_id The transaction ID, if the insert operation is part of a transaction (optional). Defaults to NULL.
 *
 * @return A BSON object representing the result of the insert operation, which includes:
 * - `"n"`: The number of documents inserted.
 * - `"ok"`: Indicates whether the operation was successful (`1.0` for success, `0.0` for failure).
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.insert(
    p_database_name text,
    p_insert __CORE_SCHEMA_V2__.bson,
    p_insert_documents __CORE_SCHEMA_V2__.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA_V2__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert$$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.insert(text,__CORE_SCHEMA_V2__.bson,__CORE_SCHEMA_V2__.bsonsequence,text)
    IS 'inserts documents into a collection for a mongo wire protocol command';

/* Command: insert */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.insert_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_document __CORE_SCHEMA_V2__.bson,
    p_transaction_id text)
 RETURNS bool
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert_one$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.insert_one(bigint,bigint,__CORE_SCHEMA_V2__.bson,text)
    IS 'internal command to insert one document into a collection';


/* Command: insert */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.insert_worker(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_shard_oid regclass,
    p_insert_internal_spec __CORE_SCHEMA_V2__.bson,
    p_insert_internal_docs __CORE_SCHEMA_V2__.bsonsequence,
    p_transaction_id text)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert_worker$$;
