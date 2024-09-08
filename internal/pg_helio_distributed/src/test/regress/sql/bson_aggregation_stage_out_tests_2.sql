SET search_path TO helio_api_catalog, helio_api, helio_core, mongo_catalog;

SET citus.next_shard_id TO 9830000;
SET helio_api.next_collection_id TO 9830;
SET helio_api.next_collection_index_id TO 9830;


-- simplest test case working with db database
SELECT helio_api.insert('db', '{"insert":"source", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

-- simple test without GUC enable
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : "target"} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- Let's enable GUC
SET helio_api.enableOutStage TO ON;

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : "target"} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('db', 'target');

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "db", "coll" : "target2" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('db', 'target2');
SELECT helio_api.drop_collection('db','source');
SELECT helio_api.drop_collection('db','target');
SELECT helio_api.drop_collection('db','target2');

-- simplest test case working
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : "target"} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('newdb', 'target');

SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "newdb", "coll" : "target2" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('newdb', 'target2');

SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');
SELECT helio_api.drop_collection('newdb','target2');

-- when source and target collection are same 
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$project" : {"data" : "updating same"} } ,{"$out" : { "db" : "newdb", "coll" : "source" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('newdb', 'source');
SELECT helio_api.drop_collection('newdb','source');

-- case where target collection already exist:
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT helio_api.insert('newdb', '{"insert":"target", "documents":[{ "_id" : 1, "data": "This is target collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "newdb", "coll" : "target" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('newdb', 'target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- when target db is different:
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" : { "db" : "db2", "coll" : "target" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM helio_api.collection('db2', 'target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('db2','target');


-- index validation: 
-- 1. If source and target are same collection, only data will get updated index should remain
SELECT helio_api.insert('newdb', '{"insert":"src", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "src", "indexes": [{ "key": {"a": 1, "b" : 1, "c" :1}, "name": "index_1" }] }'::helio_core.bson, true);
SELECT helio_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "src", "indexes": [{ "key": {"his is some index" :1}, "name": "index_2" }] }'::helio_core.bson, true);


SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "src" }') ORDER BY 1;
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "src", "pipeline": [ {"$project" : {"data" : "updating data"} }, {"$out" : { "db" : "newdb", "coll" : "src" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "src" }') ORDER BY 1;
SELECT document FROM helio_api.collection('newdb', 'src');
SELECT helio_api.drop_collection('newdb','src');


-- 2. if target already exist only data will get updated index should remain
SELECT helio_api.insert('newdb', '{"insert":"src", "documents":[{ "_id" : 1, "data": "This is source collection"}]}');
SELECT helio_api.insert('newdb', '{"insert":"tar", "documents":[{ "_id" : 1, "data": "This is target collection"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "tar", "indexes": [{ "key": {"a": 1, "b" : 1, "c" :1}, "name": "index_1" }] }'::helio_core.bson, true);
SELECT helio_api_internal.create_indexes_non_concurrently('newdb', '{ "createIndexes": "tar", "indexes": [{ "key": {"his is some index" :1}, "name": "index_2" }] }'::helio_core.bson, true);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "tar" }') ORDER BY 1;
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "src", "pipeline": [ {"$project" : {"data" : "updating data"} }, {"$out" : { "db" : "newdb", "coll" : "tar" }  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_unwind(cursorpage, '$cursor.firstBatch') FROM helio_api.list_indexes_cursor_first_page('newdb', '{ "listIndexes": "tar" }') ORDER BY 1;
SELECT document FROM helio_api.collection('newdb', 'tar');
SELECT helio_api.drop_collection('newdb','src');
SELECT helio_api.drop_collection('newdb','tar');

-- complex query with last stage $out:
SELECT helio_api.insert_one('newdb','source',' {"_id": 1, "name": "American Steak House", "food": ["filet", "sirloin"], "quantity": 100 , "beverages": ["beer", "wine"]}', NULL);
SELECT helio_api.insert_one('newdb','source','{ "_id": 2, "name": "Honest John Pizza", "food": ["cheese pizza", "pepperoni pizza"], "quantity": 120, "beverages": ["soda"]}', NULL);

SELECT helio_api.insert_one('newdb','target','{ "_id": 1, "item": "filet", "restaurant_name": "American Steak House"}', NULL);
SELECT helio_api.insert_one('newdb','target','{ "_id": 2, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "lemonade"}', NULL);
SELECT helio_api.insert_one('newdb','target','{ "_id": 3, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "soda"}', NULL);

SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "target", 
  "pipeline": 
   [ 
      { 
      "$lookup": 
         { 
            "from": "source", 
            "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}], 
            "as": "matched_docs", 
            "localField": "restaurant_name", 
            "foreignField": "name" 
         }
      },
      {"$sort"  : {"_id" :  -1}} ,
      {"$group" :{"_id" :  "$restaurant_name"}},
      {"$project" : {"matched_docs" : 0}}
   ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT document FROM helio_api.collection('newdb','target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- case when _id is missing from last stage:
SELECT helio_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$project" : {"_id" : 0} } ,{"$out" : "target"  } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT bson_dollar_project(document,'{"_id" : {"$gt" :  ["$_id", null]} }') FROM helio_api.collection('newdb','target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- test when source collection is view
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT helio_api.create_collection_view('newdb', '{ "create": "sourceView", "viewOn": "source", "pipeline": [ { "$project": { "_id": 1, "c" : {"$add" : ["$a","$b"]} } } ] }');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "sourceView", "pipeline": [  {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM  helio_api.collection('newdb', 'target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- test when source is sharded:
SELECT helio_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT helio_api.shard_collection('newdb', 'source', '{ "a": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document from helio_api.collection('newdb','target');
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- Negative test cases
SELECT helio_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : 1 } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"coll" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a", "coll" : "a", "other" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : "a", "coll" : 1} } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : {"db" : 1, "coll" : "a"} } ], "cursor": { "batchSize": 1 } }', 4294967294);

-- test when target is view:
SELECT helio_api.insert('newdb', '{"insert":"source", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT helio_api.create_collection_view('newdb', '{ "create": "targetView", "viewOn": "source", "pipeline": [ { "$project": { "_id": 1, "c" : {"$add" : ["$a","$b"]} } } ] }');
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [  {"$out" :  "targetView" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT helio_api.drop_collection('newdb','source');

-- test when target is sharded:
SELECT helio_api.insert_one('newdb','source',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT helio_api.insert_one('newdb','target',' {"_id": 1, "name": "Test Case"}', NULL);
SELECT helio_api.shard_collection('newdb', 'target', '{ "a": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "source", "pipeline": [ {"$out" : "target" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT helio_api.drop_collection('newdb','source');
SELECT helio_api.drop_collection('newdb','target');

-- let's verify we insert or update proper data in database.
--try to insert 16 MB Document
SELECT helio_api.insert_one('newdb','sourceDataValidation','{ "_id": 1, "item": "a" }' , NULL);
SELECT * FROM aggregate_cursor_first_page('newdb', FORMAT('{ "aggregate": "sourceDataValidation", "pipeline": [ {"$addFields" : { "newLargeField": "%s"} }, {"$out" : "targetDataValidation"} ], "cursor": { "batchSize": 1 } }',repeat('a', 16*1024*1024) )::bson, 4294967294);

--try to insert bad _id field
SELECT * FROM aggregate_cursor_first_page('newdb', '{ "aggregate": "sourceDataValidation", "pipeline": [ {"$project" : {"_id" : [1,2,3]}}, {"$out" : "targetDataValidation"} ], "cursor": { "batchSize": 1 } }');

-- Let's take a look at explain plan

--1) colocated tables , single shard distributed
SELECT helio_api.insert_one('newdb','explainsrc',' {"_id": 1, "name": "Test Case"}', NULL);
EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_pipeline('newdb', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');

--2) non colocated tables, hash shard distributed
SELECT helio_api.insert_one('db','explainsrc',' {"_id": 1, "name": "Test Case"}', NULL);
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');

--3) sharded source
SELECT helio_api.shard_collection('newdb', 'explainsrc', '{ "a": "hashed" }', false);
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "explainsrc", "pipeline": [{"$out" :  "target"} ] }');
