SET search_path TO documentdb_api,documentdb_core;

SET documentdb.next_collection_id TO 9200;
SET documentdb.next_collection_index_id TO 9200;

SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 1, "userName" : "Sam" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 2, "userName" : "Alex", "friend" : "Sam" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 3, "userName" : "Jamie", "friend" : "Alex" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 4, "userName" : "Taylor", "friend" : "Alex" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 5, "userName" : "Morgan", "friend" : "Jamie" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_socialgroup', '{ "_id" : 6, "userName" : "Jordan", "friend" : "Taylor" }');


SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_socialgroup", "pipeline": [ { "$graphLookup": { "from": "graphlookup_socialgroup", "startWith": "$friend", "connectFromField": "friend", "connectToField": "userName", "as": "friendChain" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_socialgroup", "pipeline": [ { "$graphLookup": { "from": "graphlookup_socialgroup", "startWith": "$friend", "connectFromField": "friend", "connectToField": "userName", "as": "friendChain" } } ]}');


SELECT documentdb_api.insert_one('db', 'graphlookup_places', '{ "_id" : 0, "placeCode" : "P1", "nearby" : [ "P2", "P3" ] }');
SELECT documentdb_api.insert_one('db', 'graphlookup_places', '{ "_id" : 1, "placeCode" : "P2", "nearby" : [ "P1", "P4" ] }');
SELECT documentdb_api.insert_one('db', 'graphlookup_places', '{ "_id" : 2, "placeCode" : "P3", "nearby" : [ "P1" ] }');
SELECT documentdb_api.insert_one('db', 'graphlookup_places', '{ "_id" : 3, "placeCode" : "P4", "nearby" : [ "P2", "P5" ] }');
SELECT documentdb_api.insert_one('db', 'graphlookup_places', '{ "_id" : 4, "placeCode" : "P5", "nearby" : [ "P4" ] }');

SELECT documentdb_api.insert_one('db', 'graphlookup_visitors', '{ "_id" : 1, "userName" : "Sam", "homePlace" : "P1" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_visitors', '{ "_id" : 2, "userName" : "Alex", "homePlace" : "P1" }');
SELECT documentdb_api.insert_one('db', 'graphlookup_visitors', '{ "_id" : 3, "userName" : "Jamie", "homePlace" : "P2" }');


SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "maxDepth": 2 } } ]}');


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "maxDepth": 2 } } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "depthField": "stepsCount" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "depthField": "stepsCount" } } ]}');


-- $graphLookup inside $facet
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_socialgroup", "pipeline": [ { "$facet": { "inner": [ { "$graphLookup": { "from": "graphlookup_socialgroup", "startWith": "$friend", "connectFromField": "friend", "connectToField": "userName", "as": "friendChain" } } ] } } ]}');


-- $graphLookup inside $lookup
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$lookup": { "from": "graphlookup_socialgroup", "as": "inner", "pipeline": [ { "$graphLookup": { "from": "graphlookup_socialgroup", "startWith": "$friend", "connectFromField": "friend", "connectToField": "userName", "as": "friendChain" } } ] } } ]}');


-- source can be sharded
SELECT documentdb_api.shard_collection('db', 'graphlookup_visitors', '{ "_id": "hashed" }', false);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "maxDepth": 2 } } ]}');


-- target cannot be sharded
SELECT documentdb_api.shard_collection('db', 'graphlookup_places', '{ "_id": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'graphlookup_socialgroup', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_socialgroup", "pipeline": [ { "$graphLookup": { "from": "graphlookup_socialgroup", "startWith": "$friend", "connectFromField": "friend", "connectToField": "userName", "as": "friendChain" } } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graphlookup_visitors", "pipeline": [ { "$graphLookup": { "from": "graphlookup_places", "startWith": "$homePlace", "connectFromField": "nearby", "connectToField": "placeCode", "as": "reachablePlaces", "maxDepth": 2 } } ]}');
