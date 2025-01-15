SET search_path TO documentdb_api,documentdb_core;

SET documentdb.next_collection_id TO 9200;
SET documentdb.next_collection_index_id TO 9200;

SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 1, "name" : "Dev" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 2, "name" : "Eliot", "reportsTo" : "Dev" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 3, "name" : "Ron", "reportsTo" : "Eliot" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 4, "name" : "Andrew", "reportsTo" : "Eliot" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 5, "name" : "Asya", "reportsTo" : "Ron" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 6, "name" : "Dan", "reportsTo" : "Andrew" }');


SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');


SELECT documentdb_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 0, "airport" : "JFK", "connects" : [ "BOS", "ORD" ] }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 1, "airport" : "BOS", "connects" : [ "JFK", "PWM" ] }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 2, "airport" : "ORD", "connects" : [ "JFK" ] }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 3, "airport" : "PWM", "connects" : [ "BOS", "LHR" ] }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 4, "airport" : "LHR", "connects" : [ "PWM" ] }');

SELECT documentdb_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 1, "name" : "Dev", "nearestAirport" : "JFK" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 2, "name" : "Eliot", "nearestAirport" : "JFK" }');
SELECT documentdb_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 3, "name" : "Jeff", "nearestAirport" : "BOS" }');


SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "depthField": "depth" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "depthField": "depth" } } ]}');


-- $graphLookup inside $facet
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$facet": { "inner": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ] } } ]}');


-- $graphLookup inside $lookup
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$lookup": { "from": "graph_lookup_employees", "as": "inner", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ] } } ]}');


-- source can be sharded
SELECT documentdb_api.shard_collection('db', 'graph_lookup_travelers', '{ "_id": "hashed" }', false);
SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');


-- target cannot be sharded
SELECT documentdb_api.shard_collection('db', 'graph_lookup_airports', '{ "_id": "hashed" }', false);
SELECT documentdb_api.shard_collection('db', 'graph_lookup_employees', '{ "_id": "hashed" }', false);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');
