SET search_path TO documentdb_api_catalog, documentdb_api, documentdb_core, public;
SET documentdb.next_collection_id TO 3400;
SET documentdb.next_collection_index_id TO 3400;

set documentdb.enablePrimaryKeyCursorScan to on;

-- insert 100 documents
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..10 LOOP
PERFORM documentdb_api.insert_one('db', 'aggregation_cursor_txn', FORMAT('{ "_id": %s, "sk": %s, "a": "%s", "c": [ %s "d" ] }',  i, mod(i, 2), repeat('Sample', 10), repeat('"' || repeat('a', 10) || '", ', 5))::documentdb_core.bson);
END LOOP;
END;
$$;

-- create an index to force non HOT path
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "aggregation_cursor_txn", "indexes": [{"key": {"a": 1}, "name": "a_1" }]}', TRUE);

-- create a streaming cursor (that doesn't drain)
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "aggregation_cursor_txn", "batchSize": 5 }', cursorId => 4294967294);

SELECT * FROM firstPageResponse;

-- now drain it
SELECT continuation AS r1_continuation FROM firstPageResponse \gset
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    documentdb_api.cursor_get_more(database => 'db', getMoreSpec => '{ "collection": "aggregation_cursor_txn", "getMore": 4294967294, "batchSize": 6 }', continuationSpec => :'r1_continuation');

-- repeat with an update
DROP TABLE firstPageResponse;

-- create a streaming cursor (that doesn't drain)
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "aggregation_cursor_txn", "batchSize": 5 }', cursorId => 4294967294);

SELECT * FROM firstPageResponse;

-- now update all the rows
SELECT documentdb_api.update('db', '{ "update": "aggregation_cursor_txn", "updates": [ { "q": { "_id": { "$gt": 0 } }, "u": { "$set": { "someField": "Updated" } }, "multi": true } ] }');

-- now drain it - should still drain.
SELECT continuation AS r1_continuation FROM firstPageResponse \gset
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    documentdb_api.cursor_get_more(database => 'db', getMoreSpec => '{ "collection": "aggregation_cursor_txn", "getMore": 4294967294, "batchSize": 6 }', continuationSpec => :'r1_continuation');

SELECT documentdb_api.shard_collection('db', 'aggregation_cursor_txn', '{ "sk": "hashed" }', FALSE);


-- repeat with sharded setup
-- repeat with an update
DROP TABLE firstPageResponse;

-- create a streaming cursor (that doesn't drain)
CREATE TEMP TABLE firstPageResponse AS
SELECT bson_dollar_project(cursorpage, '{ "cursor.firstBatch._id": 1, "cursor.id": 1 }'), continuation, persistconnection, cursorid FROM
    documentdb_api.find_cursor_first_page(database => 'db', commandSpec => '{ "find": "aggregation_cursor_txn", "batchSize": 5 }', cursorId => 4294967294);

SELECT * FROM firstPageResponse;

-- now update all the rows
SELECT documentdb_api.update('db', '{ "update": "aggregation_cursor_txn", "updates": [ { "q": { "_id": { "$gt": 0 } }, "u": { "$set": { "someField": "Updated" } }, "multi": true } ] }');

-- now drain it - should still drain.
SELECT continuation AS r1_continuation FROM firstPageResponse \gset
SELECT bson_dollar_project(cursorpage, '{ "cursor.nextBatch._id": 1, "cursor.id": 1 }'), continuation FROM
    documentdb_api.cursor_get_more(database => 'db', getMoreSpec => '{ "collection": "aggregation_cursor_txn", "getMore": 4294967294, "batchSize": 6 }', continuationSpec => :'r1_continuation');
