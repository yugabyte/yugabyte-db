SET search_path TO documentdb_api,documentdb_core,documentdb_api_internal;
SET documentdb.next_collection_id TO 6500;
SET documentdb.next_collection_index_id TO 6500;

-- first create a collection (invalid)
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "capped": true }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "capped": 1}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "capped": 2.3}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "capped": "true"}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "capped": "false"}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "timeseries": { "timeField": "a" } }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "clusteredIndex": { } }');
SELECT documentdb_api.create_collection_view('db', '{ "create": 2 }');
SELECT documentdb_api.create_collection_view('db', '{ "create": false }');
SELECT documentdb_api.create_collection_view('db', '{ "create": null }');
SELECT documentdb_api.create_collection_view('db', '{ "create": {"$undefined": true} }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "" }');   -- empty string not allowed

-- create valid collections
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_not_capped1", "capped": false}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_not_capped2", "capped": 0}');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_emoji_👽" }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests" }');

-- noops
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests" }');

-- Views error out if the viewOn is not a string, or empty string
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view", "viewOn": 2 }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view", "viewOn": false }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view", "viewOn": "" }');   -- empty string not allowed

-- viewOn can be null or undefined. It is treated as if it was not specified
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_n",  "viewOn": null }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_np", "viewOn": null, "pipeline": [] }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_u",  "viewOn": {"$undefined": true} }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_up", "viewOn": {"$undefined": true}, "pipeline": [] }');


-- create a view against it (no pipeline): succeeds
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_1", "viewOn": "create_view_tests" }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_emoji_👺", "viewOn": "create_view_tests_emoji_👽" }');

-- chain the view
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_2", "viewOn": "create_view_tests_view_1", "pipeline": [ { "$match": { "a": { "$gt": 5 } } } ] }');

-- chain one more view
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests_view_3", "viewOn": "create_view_tests_view_2", "pipeline": [ { "$sort": { "a": 1 } } ] }');

-- query all 4
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_3" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_2" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_1" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests" }');

-- now insert 3 docs in the table 
SELECT documentdb_api.insert_one('db', 'create_view_tests', '{ "_id": 1, "a": 4 }');
SELECT documentdb_api.insert_one('db', 'create_view_tests', '{ "_id": 2, "a": 1 }');
SELECT documentdb_api.insert_one('db', 'create_view_tests', '{ "_id": 3, "a": 140 }');
SELECT documentdb_api.insert_one('db', 'create_view_tests', '{ "_id": 4, "a": 40 }');

SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_3" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_2" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_1" }');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests" }');


-- create views with different options
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "viewOn": "create_view_tests_view_5", "pipeline": [ { "$sort": { "a": 1 } } ] }');


-- Can't write to views
SELECT documentdb_api.insert_one('db', 'create_view_tests_view_1', '{ "_id": 5, "a": 60 }');
SELECT documentdb_api.update('db', '{ "update": "create_view_tests_view_1", "updates": [ { "q": {}, "u": {} } ] }');
SELECT documentdb_api.delete('db', '{ "delete": "create_view_tests_view_1", "deletes": [ { "q": {}, "limit": 1 } ] }');

SELECT documentdb_api.shard_collection('db', 'create_view_tests_view_1', '{ "_id": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'create_view_tests_view_1', '{ "_id": "hashed" }', true);

-- can drop/rename a view
SELECT documentdb_api.rename_collection('db', 'create_view_tests_view_1', 'create_view_tests_view_4');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_1" }');
SELECT documentdb_api.drop_collection('db', 'create_view_tests_view_3');

-- drop the collection (view still works)
SELECT documentdb_api.drop_collection('db', 'create_view_tests');
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_1" }');

-- create a view cycle (fails)
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_tests", "viewOn": "create_view_tests_view_1" }');

-- recreate a collection
SELECT documentdb_api.insert_one('db', 'create_view_tests', '{ "_id": 3, "a": 140 }');

-- view works again
SELECT document FROM documentdb_api_catalog.bson_aggregation_find('db', '{ "find": "create_view_tests_view_1" }');


-- do a coll_mod
SELECT database_name, collection_name, view_definition FROM documentdb_api_catalog.collections WHERE collection_name = 'create_view_tests_view_1';
SELECT documentdb_api.coll_mod('db', 'create_view_tests_view_1', '{ "collMod": "create_view_tests_view_1", "viewOn": "create_view_tests_view_4", "pipeline": [] }');
SELECT database_name, collection_name, view_definition FROM documentdb_api_catalog.collections WHERE collection_name = 'create_view_tests_view_1';


-- create a much longer cycle
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_cycle_1", "viewOn": "create_view_cycle_2" }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_cycle_2", "viewOn": "create_view_cycle_3" }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_cycle_3", "viewOn": "create_view_cycle_4" }');
SELECT documentdb_api.create_collection_view('db', '{ "create": "create_view_cycle_4", "viewOn": "create_view_cycle_1" }');


-- create with long name
SELECT documentdb_api.create_collection_view('db', FORMAT('{ "create": "create_view_cycle_4_%s", "viewOn": "create_view_cycle_1" }', repeat('1bc', 80))::documentdb_core.bson);