SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;

SET citus.next_shard_id TO 9830000;
SET documentdb.next_collection_id TO 9830;
SET documentdb.next_collection_index_id TO 9830;


-- simplest test case working with db database
SELECT documentdb_api.insert('db', '{"insert":"source", "documents":[
   { "_id" : 1, "movie": "Iron Man 3", "Budget": 180000000, "year": 2011 },
   { "_id" : 2, "movie": "Captain America the winter soldier", "Budget": 170000000, "year": 2011 },
   { "_id" : 3, "movie": "Aveneger Endgame", "Budget": 160000000, "year": 2012 },
   { "_id" : 4, "movie": "Spider Man", "Budget": 150000000, "year": 2012 },
   { "_id" : 5, "movie": "Iron Man 2", "Budget": 140000000, "year": 2013 },
   { "_id" : 6, "movie": "Iron Man 1", "Budget": 130000000, "year": 2013 }
]}');


SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : "target"} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'target');

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "db", "coll" : "target2" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'target2');
SELECT documentdb_api.drop_collection('db','source');
SELECT documentdb_api.drop_collection('db','target');
SELECT documentdb_api.drop_collection('db','target2');

-- simplest test case working
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[
   { "_id" : 1, "movie": "Iron Man 3", "Budget": 180000000, "year": 2011 },
   { "_id" : 2, "movie": "Captain America the winter soldier", "Budget": 170000000, "year": 2011 },
   { "_id" : 3, "movie": "Aveneger Endgame", "Budget": 160000000, "year": 2012 },
   { "_id" : 4, "movie": "Spider Man", "Budget": 150000000, "year": 2012 },
   { "_id" : 5, "movie": "Iron Man 2", "Budget": 140000000, "year": 2013 },
   { "_id" : 6, "movie": "Iron Man 1", "Budget": 130000000, "year": 2013 }
]}');

SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : "target"} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('newdb', 'target');

SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "newdb", "coll" : "target2" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('newdb', 'target2');

SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');
SELECT documentdb_api.drop_collection('newdb','target2');

-- when source and target collection are same 
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$project" : {"data" : "updating same"} } ,{"$out" : { "db" : "newdb", "coll" : "source" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('newdb', 'source');
SELECT documentdb_api.drop_collection('newdb','source');

-- case where target collection already exist:
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT documentdb_api.insert('newdb', '{"insert":"target", "documents":[{ "_id" : 1, "data": "This is target collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "newdb", "coll" : "target" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('newdb', 'target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- when target db is different:
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "db2", "coll" : "target" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db2', 'target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('db2','target');


-- index validation: 
-- 1. If source and target are same collection, only data will get updated index should remain
SELECT documentdb_api.insert('newdb', '{"insert":"src", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "src", "indexes": [{ "key": {"a": 1, "b" : 1, "c" :1}, "name": "index_1" }] }'::documentdb_core.bson, true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "src", "indexes": [{ "key": {"his is some index" :1}, "name": "index_2" }] }'::documentdb_core.bson, true);


SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "src" }') ORDER BY 1;
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "src", "pipeline": [ {"$project" : {"data" : "updating data"} }, {"$out" : { "db" : "newdb", "coll" : "src" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "src" }') ORDER BY 1;
SELECT document FROM documentdb_api.collection('newdb', 'src');
SELECT documentdb_api.drop_collection('newdb','src');


-- 2. if target already exist only data will get updated index should remain
SELECT documentdb_api.insert('newdb', '{"insert":"src", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT documentdb_api.insert('newdb', '{"insert":"tar", "documents":[{ "_id" : 1, "data": "This is target collection"}]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "tar", "indexes": [{ "key": {"a": 1, "b" : 1, "c" :1}, "name": "index_1" }] }'::documentdb_core.bson, true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "tar", "indexes": [{ "key": {"his is some index" :1}, "name": "index_2" }] }'::documentdb_core.bson, true);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "tar" }') ORDER BY 1;
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "src", "pipeline": [ {"$project" : {"data" : "updating data"} }, {"$out" : { "db" : "newdb", "coll" : "tar" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM documentdb_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "tar" }') ORDER BY 1;
SELECT document FROM documentdb_api.collection('newdb', 'tar');
SELECT documentdb_api.drop_collection('newdb','src');
SELECT documentdb_api.drop_collection('newdb','tar');

-- complex query with last stage $out:
SELECT documentdb_api.insert_one('newdb','source',' {"_id": 1, "company": "Company A", "products": ["product1", "product2"], "amount": 100 , "services": ["service1", "service2"]}', NULL);
SELECT documentdb_api.insert_one('newdb','source','{ "_id": 2, "company": "Company B", "products": ["product3", "product4"], "amount": 120, "services": ["service3"]}', NULL);

SELECT documentdb_api.insert_one('newdb','target','{ "_id": 1, "item": "product1", "company_name": "Company A"}', NULL);
SELECT documentdb_api.insert_one('newdb','target','{ "_id": 2, "item": "product3", "company_name": "Company B", "extra": "value1"}', NULL);
SELECT documentdb_api.insert_one('newdb','target','{ "_id": 3, "item": "product3", "company_name": "Company B", "extra": "value2"}', NULL);



SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "target", 
  "pipeline": 
   [ 
      { 
      "$lookup": 
         { 
            "from": "source", 
            "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}], 
            "as": "joined_docs", 
            "localField": "company_name", 
            "foreignField": "company" 
         }
      },
      {"$sort"  : {"_id" :  -1}} ,
      {"$group" :{"_id" :  "$company_name"}},
      {"$project" : {"matched_docs" : 0}}
   ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT document FROM documentdb_api.collection('newdb','target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- case when _id is missing from last stage:
SELECT documentdb_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$project" : {"_id" : 0} } ,{"$out" : "target"  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_project(document,'{"_id" : {"$gt" :  ["$_id", null]} }') FROM documentdb_api.collection('newdb','target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- test when source collection is view
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT documentdb_api.create_collection_view('newdb', '{ "create": "sourceView", "viewOn": "source", "pipeline": [ { "$project": { "_id": 1, "c" : {"$add" : ["$a","$b"]} } } ] }');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "sourceView", "pipeline": [  {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM  documentdb_api.collection('newdb', 'target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- test when source is sharded:
SELECT documentdb_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT documentdb_api.shard_collection('newdb', 'source', '{ "a": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document from documentdb_api.collection('newdb','target');
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- Negative test cases
SELECT documentdb_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : 1 } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"coll" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a", "coll" : "a", "other" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a", "coll" : 1} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : 1, "coll" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);

-- test when target is view:
SELECT documentdb_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT documentdb_api.create_collection_view('newdb', '{ "create": "targetView", "viewOn": "source", "pipeline": [ { "$project": { "_id": 1, "c" : {"$add" : ["$a","$b"]} } } ] }');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" :  "targetView" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT documentdb_api.drop_collection('newdb','source');

-- test when target is sharded:
SELECT documentdb_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT documentdb_api.insert_one('newdb','target',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT documentdb_api.shard_collection('newdb', 'target', '{ "a": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT documentdb_api.drop_collection('newdb','source');
SELECT documentdb_api.drop_collection('newdb','target');

-- let's verify we insert or update proper data in database.
--try to insert 16 MB Document
SELECT documentdb_api.insert_one('newdb','sourceDataValidation','{ "_id": 1, "item": "a" }' , NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', FORMAT('{ "aggregate": "sourceDataValidation", "pipeline": [ {"$addFields" : { "newLargeField": "%s"} }, {"$out" : "targetDataValidation"} ], "cursor": { "batchSize": 1 } }',repeat('a', 16*1024*1024) )::bson, 4294967294);

--try to insert bad _id field
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "sourceDataValidation", "pipeline": [ {"$project" : {"_id" : [1,2,3]}}, {"$out" : "targetDataValidation"} ], "cursor": { "batchSize": 1 } }');

-- Let's take a look at explain plan

--1) colocated tables , single shard distributed
SELECT documentdb_api.insert_one('newdb','explainsrc',' {"_id": 1, "name": "Test Case"}', NULL);
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('newdb', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');

--2) non colocated tables, hash shard distributed
SELECT documentdb_api.insert_one('db','explainsrc',' {"_id": 1, "name": "Test Case"}', NULL);
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');

--3) sharded source
SELECT documentdb_api.shard_collection('newdb', 'explainsrc', '{ "a": "hashed" }', false);
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');

-- simplest test case working with db database
SELECT documentdb_api.insert('db', '{"insert":"source", "documents":[
   { "_id" : 11, "movie": "Thor", "Budget": 123456, "year": 1987 }
]}');

--validate empty collection check
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : ""} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- out should fail when query has mutable function, if query non existent collection or query has $sample stage
SELECT * FROM  aggregate_cursor_first_page('db', '{ "aggregate": "nonImmutable", "pipeline": [ {"$lookup": {"from": "bar", "as": "x", "localField": "f_id", "foreignField": "_id"}}, {"$out" :  "bar" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM  aggregate_cursor_first_page('db', '{ "aggregate": "nonImmutable", "pipeline": [ { "$sample": { "size": 1000000 } }, {"$out" :  "bar" } ], "cursor": { "batchSize": 1 } }', 4294967294);