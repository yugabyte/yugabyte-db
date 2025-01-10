SET search_path TO helio_api,helio_core;

SET helio_api.next_collection_id TO 9200;
SET helio_api.next_collection_index_id TO 9200;

SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 1, "name" : "Dev" }');
SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 2, "name" : "Eliot", "reportsTo" : "Dev" }');
SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 3, "name" : "Ron", "reportsTo" : "Eliot" }');
SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 4, "name" : "Andrew", "reportsTo" : "Eliot" }');
SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 5, "name" : "Asya", "reportsTo" : "Ron" }');
SELECT helio_api.insert_one('db', 'graph_lookup_employees', '{ "_id" : 6, "name" : "Dan", "reportsTo" : "Andrew" }');


SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');


SELECT helio_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 0, "airport" : "JFK", "connects" : [ "BOS", "ORD" ] }');
SELECT helio_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 1, "airport" : "BOS", "connects" : [ "JFK", "PWM" ] }');
SELECT helio_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 2, "airport" : "ORD", "connects" : [ "JFK" ] }');
SELECT helio_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 3, "airport" : "PWM", "connects" : [ "BOS", "LHR" ] }');
SELECT helio_api.insert_one('db', 'graph_lookup_airports', '{ "_id" : 4, "airport" : "LHR", "connects" : [ "PWM" ] }');

SELECT helio_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 1, "name" : "Dev", "nearestAirport" : "JFK" }');
SELECT helio_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 2, "name" : "Eliot", "nearestAirport" : "JFK" }');
SELECT helio_api.insert_one('db', 'graph_lookup_travelers', '{ "_id" : 3, "name" : "Jeff", "nearestAirport" : "BOS" }');


SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');


EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "depthField": "depth" } } ]}');

EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "depthField": "depth" } } ]}');


-- $graphLookup inside $facet
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$facet": { "inner": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ] } } ]}');


-- $graphLookup inside $lookup
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$lookup": { "from": "graph_lookup_employees", "as": "inner", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ] } } ]}');


-- source can be sharded
SELECT helio_api.shard_collection('db', 'graph_lookup_travelers', '{ "_id": "hashed" }', false);
SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');


-- target cannot be sharded
SELECT helio_api.shard_collection('db', 'graph_lookup_airports', '{ "_id": "hashed" }', false);
SELECT helio_api.shard_collection('db', 'graph_lookup_employees', '{ "_id": "hashed" }', false);

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_employees", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_employees", "startWith": "$reportsTo", "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ]}');

SELECT document FROM helio_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "graph_lookup_travelers", "pipeline": [ { "$graphLookup": { "from": "graph_lookup_airports", "startWith": "$nearestAirport", "connectFromField": "connects", "connectToField": "airport", "as": "destinations", "maxDepth": 2 } } ]}');
