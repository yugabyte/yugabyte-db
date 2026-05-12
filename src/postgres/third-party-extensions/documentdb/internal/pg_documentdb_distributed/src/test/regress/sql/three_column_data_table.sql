SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 2930000;
SET documentdb.next_collection_id TO 293000;
SET documentdb.next_collection_index_id TO 293000;

SET documentdb.enableDataTableWithoutCreationTime to off;
-- create table with 4 columns first
SELECT documentdb_api.create_collection('db', '4col');

\d documentdb_data.documents_293000
SELECT documentdb_api.insert_one('db', '4col', '{ "_id": 1}');


-- enable GUC
SET documentdb.enableDataTableWithoutCreationTime to on;
-- [1] let's test 4 column table after enabling GUC

-- (1.1)  insert to 4 column collection
SELECT documentdb_api.insert_one('db', '4col', '{ "_id": 2}');

-- (1.2)  multiple-insert to 4 column collection
SELECT documentdb_api.insert('db', '{"insert":"4col", "documents":[ { "_id" : 3}, { "_id" : 4}, { "_id" : 5}]}');

-- (1.3)  update to 4 column collection
SELECT documentdb_api.update('db', '{"update":"4col", "updates":[{"q":{"_id":{"$eq":1}},"u":[{"$set":{"a" : 1} }]}]}');

-- (1.4)  aggregate to 4 column collection
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "4col", "pipeline": [ {"$match" : {}} ] , "cursor": { "batchSize": 10 } }', 4294967294);

-- (1.5)  aggregate to 4 column collection
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "4col",  "pipeline": [  {"$project" : {"a" : "GUC IS ENABLED"}},{"$merge" : { "into": "4col",  "whenMatched" : "replace" , "whenNotMatched" : "insert" }} ] , "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "4col", "pipeline": [ {"$match" : {}} ] , "cursor": { "batchSize": 10 } }', 4294967294);


-- [2] let's test 3 column table after enabling GUC
SELECT documentdb_api.create_collection('db', '3col');

\d documentdb_data.documents_293001

-- (2.1)  insert to 3 column collection
SELECT documentdb_api.insert_one('db', '3col', '{ "_id": 2}');

-- (2.2)  multiple-insert to 3 column collection
SELECT documentdb_api.insert('db', '{"insert":"3col", "documents":[ { "_id" : 3}, { "_id" : 4}, { "_id" : 5}]}');

-- (2.3)  update to 3 column collection
SELECT documentdb_api.update('db', '{"update":"3col", "updates":[{"q":{"_id":{"$eq":1}},"u":[{"$set":{"a" : 1} }]}]}');

-- (2.4)  aggregate to 3 column collection
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "3col",  "pipeline": [  {"$project" : {"a" : "GUC IS ENABLED"}},{"$merge" : { "into": "3col",  "whenMatched" : "replace" , "whenNotMatched" : "insert" }} ] , "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "3col", "pipeline": [ {"$match" : {}} ] , "cursor": { "batchSize": 10 } }', 4294967294);

--3. let's disable GUC
SET documentdb.enableDataTableWithoutCreationTime to off;

-- (3.1)  insert to 3 column collection
SELECT documentdb_api.insert_one('db', '3col', '{ "_id": 200}');

-- (3.2)  multiple-insert to 3 column collection
SELECT documentdb_api.insert('db', '{"insert":"3col", "documents":[ { "_id" : 300}, { "_id" : 400}, { "_id" : 500}]}');

-- (3.3)  update to 3 column collection
SELECT documentdb_api.update('db', '{"update":"3col", "updates":[{"q":{"_id":{"$eq":1}},"u":[{"$set":{"a" : 1} }]}]}');

-- (3.4)  aggregate to 3 column collection
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "3col", "pipeline": [ {"$match" : {}} ] , "cursor": { "batchSize": 1 } }', 4294967294);
-- (3.5)  $merge to 4 column collection
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "3col",  "pipeline": [  {"$project" : {"a" : "GUC IS DISBALE"}},{"$merge" : { "into": "3col",  "whenMatched" : "replace" , "whenNotMatched" : "insert" }} ] , "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "3col", "pipeline": [ {"$match" : {}} ] , "cursor": { "batchSize": 10 } }', 4294967294);


-- lookup test
-- create 4 column table for lookup
SELECT documentdb_api.insert_one('db','three_column_data_table_from',' {"_id": 1, "col1": "Value1", "col2": ["item1", "item2"], "col3": 100 , "col4": ["item3", "item4"]}', NULL);
SELECT documentdb_api.insert_one('db','three_column_data_table_from','{ "_id": 2, "col1": "Value2", "col2": ["item5", "item6"], "col3": 120, "col4": ["item7"]}', NULL);
\d documentdb_data.documents_293002
-- now create table with 3 column
SET documentdb.enableDataTableWithoutCreationTime to on;
SELECT documentdb_api.insert_one('db','three_column_data_table_to','{ "_id": 1, "item": "item1", "ref_col": "Value1"}', NULL);
SELECT documentdb_api.insert_one('db','three_column_data_table_to','{ "_id": 2, "item": "item5", "ref_col": "Value2", "extra_col": "extra1"}', NULL);
SELECT documentdb_api.insert_one('db','three_column_data_table_to','{ "_id": 3, "item": "item5", "ref_col": "Value2", "extra_col": "item7"}', NULL);
\d documentdb_data.documents_293003

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "three_column_data_table_to", "pipeline": [ { "$lookup": { "from": "three_column_data_table_from", "pipeline": [ { "$match": { "col3": { "$gt": 110 } }}], "as": "matched_docs", "localField": "ref_col", "foreignField": "col1" }} ], "cursor": {} }');
