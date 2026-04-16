SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 7850000;
SET documentdb.next_collection_id TO 7850;
SET documentdb.next_collection_index_id TO 7850;

--Test without Enabling feature flag of $merge
SELECT documentdb_api.insert_one('sourceDB','withoutFlag',' { "_id" :  1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT * FROM aggregate_cursor_first_page('sourceDB', '{ "aggregate": "withoutFlag", "pipeline": [ {"$merge" : { "into": "targetwithoutFlag" }} ] , "cursor": { "batchSize": 1 } }', 4294967294);

--set Feature flag for $merge across DB support
SET documentdb.enableMergeAcrossDB TO ON;

--Test without Enabling flag for $merge stage target collection creation 
SELECT * FROM aggregate_cursor_first_page('sourceDB', '{ "aggregate": "withoutFlag", "pipeline": [ {"$merge" : { "into": "targetwithoutFlag" }} ] , "cursor": { "batchSize": 1 } }', 4294967294);

--set Feature flag for $merge stage target collection creation 
SET documentdb.enableMergeTargetCreation TO ON;
-- custom udf testing
SELECT * FROM documentdb_api_internal.bson_dollar_merge_add_object_id('{"_id" :  1, "a" : 1}', documentdb_api_internal.bson_dollar_merge_generate_object_id('{"a" : "dummy bson"}'));
SELECT * FROM documentdb_api_internal.bson_dollar_merge_add_object_id('{"a" : 1, "_id" :  1}', documentdb_api_internal.bson_dollar_merge_generate_object_id('{"a" : "dummy bson"}'));
SELECT count(*) FROM documentdb_api_internal.bson_dollar_merge_generate_object_id('{"a" : "dummy bson"}');
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : 1, "b" : 2}', documentdb_api_internal.bson_dollar_extract_merge_filter('{"a" : 1, "b" : 3}', 'a'::TEXT), 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : 1, "b" : 2}', documentdb_api_internal.bson_dollar_extract_merge_filter('{"a" : 2, "b" : 2}', 'a'::TEXT), 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : {"b" : 1 }, "c" : 2}', '{"a" : {"b" : 1}}', 'a.b'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : {"b" : 1 }, "c" : 2}', '{"a" : {"b" : 2}}', 'a.b'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : [1,2,3], "c" : 2}', '{"a" : 2}', 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : [1,2,3], "c" : 2}', '{"a" : 4}', 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : 1, "b" : 2}', documentdb_api_internal.bson_dollar_extract_merge_filter('{"a" : [1,2,3], "b" : 3}','a'::TEXT), 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : 1, "b" : 2}',  documentdb_api_internal.bson_dollar_extract_merge_filter('{"b" : 3}', 'a'::TEXT), 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_join('{"a" : 1, "b" : 2}',  documentdb_api_internal.bson_dollar_extract_merge_filter('{"a" : null}','a' ::TEXT), 'a'::TEXT);
SELECT * FROM documentdb_api_internal.bson_dollar_merge_fail_when_not_matched('{"a" : "this is a dummy bson"}', 'this is dummy text'::TEXT);

--  Merge with default option
SELECT documentdb_api.insert_one('defDb','defSrc',' { "_id" :  1, "a" : 1 }', NULL);
SELECT documentdb_api.insert_one('defDb','defSrc',' { "_id" :  2, "a" : 2 }', NULL);
SELECT documentdb_api.insert_one('defDb','defTar',' { "_id" :  1, "b" : 2 }', NULL);
SELECT * FROM aggregate_cursor_first_page('defDb', '{ "aggregate": "defSrc", "pipeline": [  {"$merge" : { "into": "defTar" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('defDb', 'defTar');



-- simplest test case working
SELECT documentdb_api.insert('db', '{"insert":"salaries", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');


SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "salaries", "pipeline": [  {"$merge" : { "into": "budgets" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'budgets');

-- simple test case target database is not same as source database
SELECT documentdb_api.insert_one('sourceDB','sourceDumpData',' { "_id" :  1, "item" : "almonds", "price" : 12, "quantity" : 2 }', NULL);
SELECT * FROM aggregate_cursor_first_page('sourceDB', '{ "aggregate": "sourceDumpData", "pipeline": [ {"$merge" : { "into": {"db" : "targetDB" , "coll" : "targetDumpData"}, "on" : "_id", "whenMatched" : "replace" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('targetDB', 'targetDumpData');

-- object id missing test
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "salaries", "pipeline": [ {"$project" : {"_id" : 0}}, {"$merge" : { "into": "budgets2" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT count(document) FROM documentdb_api.collection('db', 'budgets2');

-- test when merge input is string 
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "salaries", "pipeline": [ {"$merge" : "normalString" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "salaries", "pipeline": [ {"$merge" : "dotted.String" } ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','normalString');
SELECT document FROM documentdb_api.collection('db', 'dotted.String');
-- unique index test
SELECT documentdb_api.insert_one('db','uniqueIndexTestSrc',' { "_id" :  10, "a" : 1, "b" : 1, "c" : 1 , "d" : 1, "e" : 1, "f" : 1}', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "uniqueIndexTestTarget", "indexes": [{"key": {"a": 1, "b" : 1, "c" :1}, "name": "index_1", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "uniqueIndexTestTarget", "indexes": [{"key": {"a": 1}, "name": "index_2", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "uniqueIndexTestTarget", "indexes": [{"key": {"e": 1}, "name": "index_3", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "uniqueIndexTestTarget", "indexes": [{"key": {"f": 1}, "name": "index_4", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "uniqueIndexTestSrc", "pipeline": [ {"$merge" : { "on" : ["b" , "c" , "a"],"into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "uniqueIndexTestSrc", "pipeline": [ {"$merge" : { "on" : ["c" , "b" , "a"],"into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "uniqueIndexTestSrc", "pipeline": [ {"$merge" : { "on" : "_id", "into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "uniqueIndexTestSrc", "pipeline": [ {"$merge" : { "into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "uniqueIndexTestSrc", "pipeline": [ {"$merge" : { "on" : ["_id"], "into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'uniqueIndexTestTarget');

-- _id unique index test
SELECT documentdb_api.insert_one('db','objectIDUniqueIndex',' { "_id" :  10, "a" : 1, "b" : 1, "c" : 1 , "d" : 1, "e" : 1, "f" : 1, "field1" : 1, "field2" : 2}', NULL);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "uniqueIndexTestTarget", "indexes": [{"key": {"field1": 1, "field2" : 1, "_id" : 1}, "name": "index_id", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDUniqueIndex", "pipeline": [ {"$merge" : { "on" : "_id","into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDUniqueIndex", "pipeline": [ {"$merge" : { "on" : ["_id"],"into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDUniqueIndex", "pipeline": [ {"$merge" : { "on" : ["field1","_id","field2"],"into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDUniqueIndex", "pipeline": [ {"$merge" : { "into": "uniqueIndexTestTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);

--test source collection is sharded.
SELECT documentdb_api.insert('db', '{"insert":"soruceShardedTest", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

SELECT documentdb_api.shard_collection('db', 'soruceShardedTest', '{ "employee": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "soruceShardedTest", "pipeline": [ {"$merge" : { "into": "soruceShardedTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'soruceShardedTarget');

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "soruceShardedTest", "pipeline": [{"$project" : { "_id" : 1, "employee" : 1, "salary" : {"$add" : ["$salary" , 1000]} }} ,{"$merge" : { "into": "soruceShardedTarget", "whenMatched" : "replace" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'soruceShardedTarget');


-- simple replace scenario
SELECT documentdb_api.insert('db', '{"insert":"sourceDataReplace", "documents":[
   { "_id" : 1, "a": 1, "b": "a" },
   { "_id" : 2, "a": 2, "b": "b" },
   { "_id" : 3, "a": 3, "b": "c" }
]}');


SELECT documentdb_api.insert('db', '{"insert":"targetDataReplace", "documents":[
   { "_id" : 1, "a": 10 },
   { "_id" : 3, "a": 30 },
   { "_id" : 4, "a": 40 }
]}');


SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataReplace", "pipeline": [  {"$merge": {"into": "targetDataReplace", "whenMatched": "replace", "whenNotMatched": "discard"}} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','targetDataReplace');

-- let's create index and match on other key and try to replace _id field when source and target ID are same
SELECT documentdb_api.insert('db', '{"insert":"sourceDataReplaceId", "documents":[{ "_id" : 1, "a": 10 }]}');
SELECT documentdb_api.insert('db', '{"insert":"targetDataReplaceId", "documents":[{ "_id" : 1, "a": 10, "b" : 10 }]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataReplaceId", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataReplaceId", "pipeline": [ {"$merge" : { "into": "targetDataReplaceId", "on" : "a", "whenMatched" : "replace" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','targetDataReplaceId');

-- let's create index and match on other key and try to replace _id field whe source and target ID's are different. 
SELECT documentdb_api.insert('db', '{"insert":"sourceDataReplaceIdDiff", "documents":[{ "_id" : 1, "a": 10 }]}');
SELECT documentdb_api.insert('db', '{"insert":"targetDataReplaceIdDiff", "documents":[{ "_id" : 2, "a": 10, "b" : 10 }]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataReplaceIdDiff", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataReplaceIdDiff", "pipeline": [ {"$merge" : { "into": "targetDataReplaceIdDiff", "on" : "a", "whenMatched" : "replace" }} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- Now let's see what happens when we are not projecting _id from source and source _id is getting generated and if source objectID is generated then it will be not same as targets.
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataReplaceIdDiff", 
"pipeline": [  {"$project" : { "_id": 0 }},
               {"$merge" : { "into": "targetDataReplaceIdDiff", "on" : "a", "whenMatched" : "replace" }} 
            ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT document FROM documentdb_api.collection('db','targetDataReplaceIdDiff');


-- simple merge scenario
SELECT documentdb_api.insert('db', '{"insert":"sourceWhenMatchMerge", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

SELECT documentdb_api.insert('db', '{"insert":"targetWhenMatchMerge", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

-- As all doc from source matches with target so there should not be any change in target collection. 
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceWhenMatchMerge", "pipeline": [  {"$merge" : { "into": "targetWhenMatchMerge", "whenMatched" : "merge" }} ], "cursor": { "batchSize": 1 } }', 4294967294);

select document from documentdb_api.collection('db', 'targetWhenMatchMerge');

-- Let's Add one more column by projection and see if it merges in target collection.
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceWhenMatchMerge", 
"pipeline": [ 
               {"$project" : {"_id" : 1, "new_column" : "testing merge"}}, 
               {"$merge" : { "into": "targetWhenMatchMerge", "whenMatched" : "merge" }} 
            ], 
"cursor": { "batchSize": 1 } }', 4294967294);
select document from documentdb_api.collection('db', 'targetWhenMatchMerge');

-- let's try to modify value of existing column of target collection. 
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceWhenMatchMerge", 
"pipeline": [ 
               {"$project" : {"_id" : 1, "fiscal_year" : {"$add" : ["$fiscal_year" , 1]}}}, 
               {"$merge" : { "into": "targetWhenMatchMerge", "whenMatched" : "merge" }} 
            ], 
"cursor": { "batchSize": 1 } }', 4294967294);
select document from documentdb_api.collection('db', 'targetWhenMatchMerge');

-- let's create index and match on other key and try to merge when _id field on source and target ID are same
SELECT documentdb_api.insert('db', '{"insert":"sourceDataMergeId", "documents":[{ "_id" : 1, "a": 10, "b" : 10 }]}');
SELECT documentdb_api.insert('db', '{"insert":"targetDataMergeId", "documents":[{ "_id" : 1, "a": 10  }]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataMergeId", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataMergeId", "pipeline": [ {"$merge" : { "into": "targetDataMergeId", "on" : "a", "whenMatched" : "merge" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','targetDataMergeId');

-- let's create index and match on other key and try to merge when _id field on source and target ID are different
SELECT documentdb_api.insert('db', '{"insert":"sourceDataMergeIdDiff", "documents":[{ "_id" : 1, "a": 10, "b" : 10 }]}');
SELECT documentdb_api.insert('db', '{"insert":"targetDataMergeIdDiff", "documents":[{ "_id" : 2, "a": 10  }]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataMergeIdDiff", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataMergeIdDiff", "pipeline": [ {"$merge" : { "into": "targetDataMergeIdDiff", "on" : "a", "whenMatched" : "merge" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','targetDataMergeIdDiff');

-- Now let's see what happens when we are not projecting _id from source and source _id is getting generated and if source objectID is generated then it will be not same as target's.
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataMergeIdDiff", 
"pipeline": [  {"$project" : { "_id": 0 }},
               {"$merge" : { "into": "targetDataMergeIdDiff", "on" : "a", "whenMatched" : "merge" }} 
            ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT document FROM documentdb_api.collection('db','targetDataMergeIdDiff');

-- merge whenNotMatched fail
-- 1) this will not fail as all the doc from source are matching with target
SELECT documentdb_api.insert('db', '{"insert":"sourceNotMatchedFailed", "documents":[
   { "_id" : 1, "a": 1, "b": "a" },
   { "_id" : 2, "a": 2, "b": "b" },
   { "_id" : 3, "a": 3, "b": "c" }
]}');

SELECT documentdb_api.insert('db', '{"insert":"targetNotMatchedFailed", "documents":[
   { "_id" : 1, "a": 1, "b": "e" },
   { "_id" : 2, "a": 2, "b": "f" },
   { "_id" : 3, "a": 3, "b": "g" }
]}');

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceNotMatchedFailed", 
                  "pipeline": [  
                                 {"$merge" : 
                                       { "into": "targetNotMatchedFailed", 
                                        "whenMatched" : "replace" ,
                                        "whenNotMatched" : "fail" }
                                 } 
                              ], 
                           "cursor": { "batchSize": 1 } 
               }',  4294967294
            );

select document FROM documentdb_api.collection('db','targetNotMatchedFailed');

-- 2) this will fail as not one source doc is not matching with target and user has selected failure option.
SELECT documentdb_api.insert_one('db','sourceNotMatchedFailed',' { "_id" :  10, "a" : 1, "b" : 1 }', NULL);

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceNotMatchedFailed", 
                  "pipeline": [  
                                 {"$merge" : 
                                       { "into": "targetNotMatchedFailed", 
                                        "whenMatched" : "replace" ,
                                        "whenNotMatched" : "fail" }
                                 } 
                              ] , "cursor": { "batchSize": 1 } }', 4294967294);

select document FROM documentdb_api.collection('db','targetNotMatchedFailed');

-- merge whenNotMatched Discard
SELECT documentdb_api.insert('db', '{"insert":"sourceNotMatchedDiscard", "documents":[
   { "_id" : 1, "a": 1, "b": "a" },
   { "_id" : 2, "a": 2, "b": "b" },
   { "_id" : 3, "a": 3, "b": "c" }
]}');

SELECT documentdb_api.insert('db', '{"insert":"targetNotMatchedDiscard", "documents":[
   { "_id" : 1, "a": 1, "b": "e" },
   { "_id" : 5, "a": 2, "b": "f" },
   { "_id" : 6, "a": 3, "b": "g" }
]}');

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceNotMatchedDiscard", 
                  "pipeline": [  
                                 {"$merge" : 
                                       { "into": "targetNotMatchedDiscard", 
                                        "whenMatched" : "replace" ,
                                        "whenNotMatched" : "discard" }
                                 } 
                              ] , "cursor": { "batchSize": 1 } }', 4294967294);


select document FROM documentdb_api.collection('db','targetNotMatchedDiscard');

-- WhanMathched : keepexisting
SELECT documentdb_api.insert('db', '{"insert":"sourceMatchedKeepExisting", "documents":[
   { "_id" : 1, "a": 1, "b": "a" },
   { "_id" : 2, "a": 2, "b": "b" },
   { "_id" : 3, "a": 3, "b": "c" }
]}');

SELECT documentdb_api.insert('db', '{"insert":"targetMatchedKeepExisting", "documents":[
   { "_id" : 1, "a": 1, "b": "e" },
   { "_id" : 2, "a": 2, "b": "f" },
   { "_id" : 6, "a": 3, "b": "g" }
]}');

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceMatchedKeepExisting", 
                  "pipeline": [  
                                 {"$merge" : 
                                       { "into": "targetMatchedKeepExisting", 
                                        "whenMatched" : "keepExisting" ,
                                        "whenNotMatched" : "insert" }
                                 } 
                              ] , "cursor": { "batchSize": 1 } }', 4294967294);
select document from documentdb_api.collection('db','targetMatchedKeepExisting');


-- WhanMathched : fail

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceMatchedKeepExisting", 
                  "pipeline": [  
                                 {"$merge" : 
                                       { "into": "targetMatchedKeepExisting", 
                                        "whenMatched" : "fail" ,
                                        "whenNotMatched" : "insert" }
                                 } 
                              ] , "cursor": { "batchSize": 1 } }', 4294967294);
select document FROM documentdb_api.collection('db','targetMatchedKeepExisting');

-- whenMatched : _id are getting changed in case of replace
SELECT documentdb_api.insert('db', '{"insert":"sourceMatchedIdTests", "documents":[
   { "_id" : 4, "a": 1, "b": "a" },
   { "_id" : 5, "a": 2, "b": "b" },
   { "_id" : 6, "a": 3, "b": "c" }
]}');

SELECT documentdb_api.insert('db', '{"insert":"targetMatchedIdTests", "documents":[
   { "_id" : 1, "a": 1, "b": "e" },
   { "_id" : 2, "a": 2, "b": "f" },
   { "_id" : 6, "a": 3, "b": "g" }
]}');

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetMatchedIdTests", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', 
               '{ 
                  "aggregate": "sourceMatchedIdTests", 
                  "pipeline": [  
                                 {"$merge" : 
                                       {
                                        "on" : "a", 
                                        "into": "targetMatchedIdTests", 
                                        "whenMatched" : "replace" ,
                                        "whenNotMatched" : "insert" }
                                 } 
                              ] , "cursor": { "batchSize": 1 } }', 4294967294);
select document FROM documentdb_api.collection('db','targetMatchedIdTests');

-- complex pipeline with last stage merge with replace and insert mode.
SELECT documentdb_api.insert_one('db','pipelinefromMerge',' {"_id": 1, "name": "American Steak House", "food": ["filet", "sirloin"], "quantity": 100 , "beverages": ["beer", "wine"]}', NULL);
SELECT documentdb_api.insert_one('db','pipelinefromMerge','{ "_id": 2, "name": "Honest John Pizza", "food": ["cheese pizza", "pepperoni pizza"], "quantity": 120, "beverages": ["soda"]}', NULL);

SELECT documentdb_api.insert_one('db','pipelinetoMerge','{ "_id": 1, "item": "filet", "restaurant_name": "American Steak House"}', NULL);
SELECT documentdb_api.insert_one('db','pipelinetoMerge','{ "_id": 2, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "lemonade"}', NULL);
SELECT documentdb_api.insert_one('db','pipelinetoMerge','{ "_id": 3, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "soda"}', NULL);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "pipelinetoMerge", 
  "pipeline": 
   [ 
      { 
      "$lookup": 
         { 
            "from": "pipelinefromMerge", 
            "pipeline": [ { "$match": { "quantity": { "$gt": 110 } }}], 
            "as": "matched_docs", 
            "localField": "restaurant_name", 
            "foreignField": "name" 
         }
      },
      {"$sort"  : {"_id" :  -1}} ,
      {"$group" :{"_id" :  "$restaurant_name"}},
      {"$project" : {"matched_docs" : 0}},
      {"$merge" : { "into": "pipelineFinalMerge", "whenMatched": "replace", "whenNotMatched": "insert" }} 
   ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT document FROM documentdb_api.collection('db','pipelineFinalMerge');

-- when target collection has array 
SELECT documentdb_api.insert_one('db','sourceTargetHasArray',' { "_id" :  1, "a" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','sourceTargetHasArray',' { "_id" :  2, "a" : 5 }', NULL);
SELECT documentdb_api.insert_one('db','targetTargetHasArray',' { "_id" :  1, "a" : [1,2,3,4] }', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetTargetHasArray", "indexes": [{"key": {"a": 1}, "name": "index_2", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceTargetHasArray", "pipeline":[  {"$merge" : { "into": "targetTargetHasArray", "on" : "a", "whenMatched" : "replace", "whenNotMatched" : "insert" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','target');

-- when query has sort clause
SELECT documentdb_api.insert_one('db','sortClauseToTable','{ "_id": 1, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "soda"}', NULL);
SELECT documentdb_api.insert_one('db','sortClauseToTable','{ "_id": 2, "item": "cheeseLess pizza", "restaurant_name": "DisHonest John Pizza", "drink": "pepsi"}', NULL);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sortClauseToTable",  "pipeline": [  {"$sort"  : {"_id" :  -1}} , {"$merge" : { "into": "sortClauseTarget", "whenMatched": "replace", "whenNotMatched": "insert" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','sortClauseTarget');

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "pipelinetoMerge",  "pipeline": [  {"$sort"  : {"_id" :  -1}} , {"$group" : {"_id" :  "$_id"}}, {"$merge" : { "into": "sortClauseGroup", "whenMatched": "replace", "whenNotMatched": "insert" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','sortClauseGroup');


-- when query has multiple sort clause or multiple resjunk columns
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  1, "a" : 45, "b" : 10 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  2, "a" : 87, "b" : 7 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  3, "a" : 29, "b" : 17 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  4, "a" : 13, "b" : 99 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  5, "a" : 45, "b" : 113 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  6, "a" : 87, "b" : 76 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  7, "a" : 29, "b" : 86 }', NULL);
SELECT documentdb_api.insert_one('db','resJunkSrc',' { "_id" :  8, "a" : 13, "b" : 20 }', NULL);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "resJunkSrc",  "pipeline": [  {"$sort"  : {"a" :  -1, "b" : 1}} , {"$merge" : { "into": "resJunkeTarget", "whenMatched": "replace", "whenNotMatched": "insert" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db','resJunkeTarget');


-- test when source collection is view
SELECT documentdb_api.create_collection('db', 'sourceCollForView');
SELECT documentdb_api.insert('db', '{"insert":"sourceCollForView", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceCollForView", "documents":[{ "_id" : 2, "a" : 2, "b" : 2  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceCollForView", "documents":[{ "_id" : 3, "a" : 3, "b" : 3  }]}');

SELECT documentdb_api.create_collection_view('db', '{ "create": "sourceView", "viewOn": "sourceCollForView", "pipeline": [ { "$project": { "_id": 1, "c" : {"$add" : ["$a","$b"]} } } ] }');

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceView", "pipeline": [  {"$merge" : { "into" : "targetViewedOutput" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM  documentdb_api.collection('db', 'targetViewedOutput');

-- Let's look at explain plan for unshared collection
SELECT documentdb_api.create_collection('db', 'sourceExplain');
SELECT documentdb_api.insert('db', '{"insert":"sourceExplain", "documents":[{ "_id" : 1, "a" : 1, "b" : 1  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceExplain", "documents":[{ "_id" : 2, "a" : 2, "b" : 1  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceExplain", "documents":[{ "_id" : 3, "a" : 3, "b" : 1  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceExplain", "documents":[{ "_id" : 4, "a" : 4, "b" : 1  }]}');
SELECT documentdb_api.insert('db', '{"insert":"sourceExplain", "documents":[{ "_id" : 5, "a" : 5, "b" : 1  }]}');
EXPLAIN VERBOSE SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sourceExplain", "pipeline": [{"$merge" : { "into": "explainTarget", "whenMatched" : "replace" }} ] }');

--explain when merge is not first stage of pipeline
EXPLAIN VERBOSE SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sourceExplain", "pipeline": [{ "$match" : {"_id" :1}}, {"$merge" : { "into": "explainTarget", "whenMatched" : "replace" }} ] }');


-- Let's look at explain plan for sharded collection
SELECT documentdb_api.shard_collection('db', 'sourceExplain', '{ "a": "hashed" }', false);
EXPLAIN VERBOSE SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sourceExplain", "pipeline": [{"$merge" : { "into": "explainTarget", "whenMatched" : "replace" }} ] }');

-- Let's look at explain plan for when on field is indexed
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "sourceExplain", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "explainTarget", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);

-- citus will wrap our query in a subquery: SELECT document, target_shard_key_value FROM (our query) AS subquery
EXPLAIN VERBOSE SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "sourceExplain", "pipeline": [{"$merge" : { "into": "explainTarget", "on" : "a", "whenMatched" : "replace" }} ] }');

-- source and target are same
SELECT documentdb_api.create_collection('db', 'AsSourceAsTarget');
SELECT documentdb_api.insert_one('db','AsSourceAsTarget',' { "_id" :  1, "a" : 1, "b" : 1 }', NULL);
SELECT * FROM  aggregate_cursor_first_page('db', '{ "aggregate": "AsSourceAsTarget", "pipeline": [ {"$project" : {"new" : "data"}}, {"$merge" : { "into": "AsSourceAsTarget" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'AsSourceAsTarget');

-- Negative tests : Invalid input
SELECT documentdb_api.create_collection('db', 'negInvalidInput');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : 1} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : {  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "xman" : 1  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : 1  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : {"db" : "apple"}  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : {"db" : 1}  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : {"db" : "database", "coll" :  1}  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : {"db" : "apple", "haha" : "haha"}  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "on" :  1  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "on" :  [1]  }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "whenMatched" : 1   }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "whenMatched" : "falsevalue"   }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "whenNotMatched" : 1   }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl", "whenNotMatched" : "falsevalue"   }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl" }}, {} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "outColl" }}, {"project" : {"a" : 1}
} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into": "targetNotExists", "on" : [] }} ], "cursor": { "batchSize": 1 } }', 4294967294);


-- Negative test cases when on fields value are not unique index.
SELECT documentdb_api.create_collection('db', 'indexNegColl');
SELECT documentdb_api.insert_one('db','indexNegColl',' { "_id" :  1, "a" : 1, "b" : 1 }', NULL);

SELECT documentdb_api.create_collection('db', 'indexNegOutColl');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"b": 1}, "name": "index_2", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"c": 1}, "name": "index_3", "unique" : false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"d": 1}, "name": "index_A"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"e": 1, "f" : 1, "g" : 1}, "name": "index_4"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"x": 1, "y" : 1, "z" : 1}, "name": "index_5", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"parag": 1}, "name": "index_6", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"jain": 1}, "name": "index_7", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"partialTest": 1}, "name": "index_8", "unique" : true, "partialFilterExpression": {"a": {"$gt": 1}}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexNegOutColl", "indexes": [{"key": {"partialTest": 1, "partialTest2" : 1}, "name": "index_9", "unique" : true, "partialFilterExpression": {"a": {"$gt": 1}}}]}', true);

SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "c" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "d" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["b", "a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["b", "c"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["b", "d"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "e" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["f","g"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["e","f","g"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "f" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["x","z","y","f"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "x" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["x","z"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["x","_id"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["parag","jain"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["partialTest"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : "partialTest" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["partialTest", "partialTest2"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["a", "a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["a", "b", "b"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["c", "a", "c"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["x","y"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "indexNegOutColl", "on" : ["z","x"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- _id Negative unique test
SELECT documentdb_api.insert_one('db','objectIDNegUniqueIndex',' { "_id" :  10, "a" : 1, "b" : 1, "c" : 1 , "d" : 1, "e" : 1, "f" : 1}', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "objectIDTargetNegUniqueIndex", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "objectIDTargetNegUniqueIndex", "indexes": [{"key": {"b": 1}, "name": "index_2", "unique" : true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "objectIDTargetNegUniqueIndex", "indexes": [{"key": {"a": 1, "b" : 1}, "name": "index_3", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDNegUniqueIndex", "pipeline": [ {"$merge" : { "on" : ["_id", "a"],"into": "objectIDTargetNegUniqueIndex" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDNegUniqueIndex", "pipeline": [ {"$merge" : { "on" : ["a", "_id"], "into": "objectIDTargetNegUniqueIndex" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "objectIDNegUniqueIndex", "pipeline": [ {"$merge" : { "on" : ["a", "_id", "b"], "into": "objectIDTargetNegUniqueIndex" }} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- Negative tests : when target collection does not exist
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "targetNotExist", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "targetNotExist", "on" : ["a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "targetNotExist", "on" : ["_id","a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "indexNegColl", "pipeline": [  {"$merge" : { "into": "targetNotExist", "on" : ["a","_id"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);

--Negative test when target collection is a view
SELECT documentdb_api.create_collection('db', 'targetCollForView');
SELECT documentdb_api.insert('db', '{"insert":"targetCollForView", "documents":[{ "_id" : 1, "a" : 1  }]}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "targetView", "viewOn": "targetCollForView", "pipeline": [ { "$sort": { "a": 1 } } ] }');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "targetCollForView", "pipeline": [  {"$merge" : { "into" : "targetView" }} ], "cursor": { "batchSize": 1 } }', 4294967294);

--Negative tests : when on field is missing/array in source document
SELECT documentdb_api.insert('db', '{"insert":"sourceDataMissing", "documents":[{ "_id" : 3, "b": "c" }]}');
SELECT documentdb_api.insert('db', '{"insert":"targetDataMissingTest", "documents":[{ "_id" : 3, "b": "c" }]}');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataMissingTest", "indexes": [{"key": {"a": 1}, "name": "index_1", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataMissing", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataMissing", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" : ["a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT documentdb_api.insert('db', '{"insert":"sourceDataArray", "documents":[{ "_id" : 3, "a" : [1,2,3], "b": "c" }]}');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataArray", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataArray", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" :  ["a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);

SELECT documentdb_api.insert('db', '{"insert":"sourceDataNull", "documents":[{ "_id" : 3, "a" : null , "b": "c" }]}');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataNull", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataNull", "pipeline": [  {"$merge" : { "into": "targetDataMissingTest", "on" :  ["a"] }} ], "cursor": { "batchSize": 1 } }', 4294967294);

-- Negative test when non-Immutable function present in query
-- As in below query we call empty_data_table() which is non-Immutable function because targetcollection in lookup stage is empty.
SELECT documentdb_api.create_collection('db', 'nonImmutable');
SELECT documentdb_api.insert_one('db','nonImmutable',' { "_id" :  1, "a" : 1, "b" : 1 }', NULL);
SELECT * FROM  aggregate_cursor_first_page('db', '{ "aggregate": "nonImmutable", "pipeline": [ {"$lookup": {"from": "bar", "as": "x", "localField": "f_id", "foreignField": "_id"}}, {"$merge" : { "into": "bar" }} ], "cursor": { "batchSize": 1 } }', 4294967294);


-- Merge inside transaction
BEGIN;
SELECT documentdb_api.insert('db', '{"insert":"insideTransaction", "documents":[{ "_id" : 3, "a" : 3, "b": "c" }]}');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "insideTransaction", "pipeline": [  {"$merge" : { "into": "targetTransaction", "on" : "a" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
END;

-- Option not supported failures
SELECT documentdb_api.create_collection('db', 'optionNotSupported');
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "negInvalidInput", "pipeline": [  {"$merge" : { "into" : "optionNotSupportedOut", "whenMatched" : [] }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT documentdb_api.shard_collection('db', 'optionNotSupportedOut', '{ "a": "hashed" }', false);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "optionNotSupported", "pipeline": [  {"$merge" : { "into" : "optionNotSupportedOut" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "optionNotSupported", "pipeline": [  { "$match": { "name": "test" } }, {  "$graphLookup":  {"from": "optionNotSupported",  "startWith": "$id",  "connectFromField": "id", "connectToField": "ReportsTo", "as": "reportingHierarchy"}}, {"$merge":{ "into": "targetDataReplace"}}], "cursor": { "batchSize": 1 } }', 4294967294);

-- simple test case target database is not same as source database
SET documentdb.enableMergeAcrossDB TO OFF;
SELECT * FROM aggregate_cursor_first_page('sourceDB', '{ "aggregate": "sourceDumpData", "pipeline": [ {"$merge" : { "into": {"db" : "targetDB" , "coll" : "targetDumpData"}, "on" : "_id", "whenMatched" : "replace" }} ], "cursor": { "batchSize": 1 } }', 4294967294);


-- let's verify we insert or update proper data in database.
--try to insert 16 MB Document
SELECT documentdb_api.insert_one('db','sourceDataValidation','{ "_id": 1, "item": "a" }' , NULL);
SELECT * FROM aggregate_cursor_first_page('db', FORMAT('{ "aggregate": "sourceDataValidation", "pipeline": [ {"$addFields" : { "newLargeField": "%s"} }, {"$merge" : {"into" : "targetDataValidation"}} ], "cursor": { "batchSize": 1 } }',repeat('a', 16*1024*1024) )::bson, 4294967294);

--try to insert bad _id field
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataValidation", "pipeline": [ {"$project" : {"_id" : [1,2,3]}}, {"$merge" : {"into" : "targetDataValidation"}} ], "cursor": { "batchSize": 1 } }');

-- try to update 16 MB document 
SELECT documentdb_api.insert_one('db','targetDataValidation','{ "_id": 1, "item": "a" }' , NULL);
SELECT * FROM aggregate_cursor_first_page('db', FORMAT('{ "aggregate": "sourceDataValidation", "pipeline": [ {"$addFields" : { "newLargeField": "%s"} }, {"$merge" : {"into" : "targetDataValidation"}} ], "cursor": { "batchSize": 1 } }',repeat('a', 16*1024*1024) )::bson, 4294967294);

--try to update bad _id field
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "targetDataValidation", "indexes": [{"key": {"item": 1}, "name": "index_1", "unique" : true}]}', true);
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "sourceDataValidation", "pipeline": [ {"$project" : {"_id" : [1,2,3], "item" : 1}}, {"$merge" : {"into" : "targetDataValidation", "on" : "item"}} ], "cursor": { "batchSize": 1 } }');

-- Basic Test with enable Native colocation
SET documentdb.enableNativeColocation=true; 

SELECT documentdb_api.insert('db', '{"insert":"nativeSrc", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

--insert
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "nativeSrc", "pipeline": [  {"$merge" : { "into": "nativeTar" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'nativeTar');

--replace
SELECT * FROM aggregate_cursor_first_page('db', '{ "aggregate": "nativeSrc", "pipeline": [  {"$merge" : { "into": "nativeTar"}} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT document FROM documentdb_api.collection('db', 'nativeTar');

SET documentdb.enableNativeColocation=false;
