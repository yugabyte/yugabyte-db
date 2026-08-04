SET search_path TO documentdb_api,documentdb_core,documentdb_api_catalog;
SET documentdb.next_collection_id TO 2100;
SET documentdb.next_collection_index_id TO 2100;

SELECT documentdb_api.create_collection_view('db', '{ "create": "test" }');
SELECT documentdb_api.insert_one('db', 'test', '{ "_id": 1, "a": 1, "ttl": 0 }');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "test", "indexes": [ { "key": { "ttl": 1 }, "name": "ttlIndex", "expireAfterSeconds": 300 } ] }', TRUE);

-- set the default transaction mode.
set default_transaction_read_only = on;

-- should fail gracefully.
SELECT documentdb_api.create_collection_view('db', '{ "create": "collection" }');
SELECT documentdb_api.insert('db', '{ "insert": "test", "documents": [ { "a": 1 }] }');
SELECT documentdb_api.update('db', '{ "update": "test", "updates": [ { "q": { "a": 1 }, "u": { "$set": { "b": 1 }} }] }');
SELECT documentdb_api.delete('db', '{ "delete": "test", "deletes": [ { "q": { "a": 1 } }] }');

SELECT document FROM  documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "test", "pipeline": [ { "$merge": { "into": "test" } } ] }');
SELECT documentdb_api.drop_collection('db', 'collection');

-- same command works when transaction explicitly read-write.
BEGIN;
SET TRANSACTION READ WRITE;
SELECT documentdb_api.create_collection_view('db', '{ "create": "collection" }');
SELECT documentdb_api.insert('db', '{ "insert": "collection", "documents": [ { "a": 1 }] }');
SELECT documentdb_api.update('db', '{ "update": "collection", "updates": [ { "q": { "a": 1 }, "u": { "$set": { "b": 1 }} }] }');
SELECT documentdb_api.delete('db', '{ "delete": "collection", "deletes": [ { "q": { "a": 1 }, "limit": 1 }] }');
SELECT documentdb_api.drop_collection('db', 'collection');
ROLLBACK;

-- fails gracefully when transaction is read-only.
CALL documentdb_api_internal.delete_expired_rows();

set documentdb.enableTTLJobsOnReadOnly to on;
CALL documentdb_api_internal.delete_expired_rows();