SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 413000;
SET documentdb.next_collection_id TO 4130;
SET documentdb.next_collection_index_id TO 4130;


SELECT documentdb_api.insert_one('db','aggregation_pipeline_let','{"_id":"1", "int": 10, "a" : { "b" : [ "x", 1, 2.0, true ] } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let','{"_id":"2", "double": 2.0, "a" : { "b" : {"c": 3} } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let','{"_id":"3", "boolean": false, "a" : "no", "b": "yes", "c": true }', NULL);

-- fetch all rows
SELECT shard_key_value, object_id, document FROM documentdb_api.collection('db', 'aggregation_pipeline_let') ORDER BY object_id;

-- add newField
-- with let enabled
EXPLAIN (VERBOSE ON) SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": 20 } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": 20 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": { "$avg": [ 20, 40 ] } } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varNotRef" }, "filter": {}, "let": { "varRef": { "$avg": [ 20, 40 ] } } }');

-- let support in $expr
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$lt": [ "$_id", "$$varRef" ]} }, "let": { "varRef": "3" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ]} }, "let": { "varRef": 3 } }');

SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$lt": [ "$_id", "$$varNotRef" ]} }, "let": { "varRef": "3" } }');

-- let support in $expr with nested $let
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varNotRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
-- same scenario but with aggregation pipeline
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$lt": [ "$_id", "$$varRef" ]} } } ], "let": { "varRef": "3" } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } } } ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$lt": [ { "$toInt": "$_id" }, "$$varNotRef" ] } } } ], "let": { "varRef": 3 } }');

-- let support in $expr with nested $let
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varNotRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
-- same scenario but with aggregation pipeline
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$let": { "vars": { "varRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} } } ], "let": { "varRef": 3 } }');

-- find/aggregate with variables referencing other variables on the same let spec should work
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply"  }, "filter": {}, "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]} } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{"$project": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply"  }}], "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]} } }');

-- nested $let should also work
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply", "nestedLet": "$$nestedLet"  }, "filter": {}, "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]}, "nestedLet": {"$let": {"vars": {"add": {"$add": ["$$multiply", 1]}}, "in": "$$add"}} } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{"$project": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply", "nestedLet": "$$nestedLet"  }}], "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]}, "nestedLet": {"$let": {"vars": {"add": {"$add": ["$$multiply", 1]}}, "in": "$$add"}} } }');

-- if we change the order and the variable that we're referencing is defined afterwards we should fail
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply"  }, "filter": {}, "let": { "add": {"$add": ["$$varRef", 2]}, "varRef": 20, "multiply": {"$multiply": ["$$add", 2]} } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{"$project": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply"  }}], "let": { "add": {"$add": ["$$varRef", 2]}, "varRef": 20, "multiply": {"$multiply": ["$$add", 2]} } }');

-- $addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$set": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- pipeline with inlined $project then addFields, on exclusion
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$project": { "a": 0, "boolean": 0 } }, { "$addFields": { "a": "$$varRef", "xyz": "$_id" } } ], "let": {"varRef": 1}}');
EXPLAIN (COSTS OFF, VERBOSE ON ) SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$project": { "a": 0, "boolean": 0 } }, { "$addFields": { "a": 1, "xyz": "$_id" } } ], "let": {"varRef": 1}}');

-- $replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$replaceRoot": { "newRoot": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } } }} ], "let": { "varRef": 3 } }');

-- $replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$replaceWith": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- $group (with simple aggregators)
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" } } } ], "let": { "varRef": 3 } }');

-- $group (with sorted accumulators)
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" }, "first": { "$first" : "$a.b" } } } ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" }, "last": { "$last" : "$a.b" } } } ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$set": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id": "$$varRef", "top": {"$top": {"output": [ "$_id" ], "sortBy": { "_id": 1 }}}}} ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$set": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id": "$$varRef", "bottom": {"$bottom": {"output": [ "$_id" ], "sortBy": { "_id": 1 }}}}} ], "let": { "varRef": 3 } }');

-- $project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$project": { "_id": 1, "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- $unionWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": { "c": "foo" }}, { "$unionWith": { "coll": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "bar": "$$varRef" } } ] } } ], "let": { "varRef": 30 }}');

-- $sortByCount
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$sortByCount": { "$lt": [ "$_id", "$$varRef" ] } } ], "let": { "varRef": "2" } }');

-- $facet
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "newField": "myvalue" }}, { "$facet": { "sb1": [ { "$addFields": { "myVar": "$$varRef" }} ], "sb2": [ { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" } } } ] }} ], "let": { "varRef": "2" } }');

-- $graphLookup
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 1, "name" : "Dev" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 2, "name" : "Eliot", "reportsTo" : "Dev" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 3, "name" : "Ron", "reportsTo" : "Eliot" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 4, "name" : "Andrew", "reportsTo" : "Eliot" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 5, "name" : "Asya", "reportsTo" : "Ron" }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_gl', '{ "_id" : 6, "name" : "Dan", "reportsTo" : "Andrew" }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let_gl", "pipeline": [ { "$graphLookup": { "from": "aggregation_pipeline_let_gl", "startWith": { "$max": [ "$reportsTo", "$$reportsTo" ] }, "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ], "let": { "reportsTo": "Dev" } }');


-- $inverseMatch
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_inv', '{ "_id" : 1, "policy" : { "name": "Dev" } }');
SELECT documentdb_api.insert_one('db', 'aggregation_pipeline_let_inv', '{ "_id" : 1, "policy" : { "name": "Elliot" } }');

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let_inv", "pipeline": [ { "$inverseMatch": { "path": "policy", "from": "aggregation_pipeline_let_gl", "pipeline": [{"$match": {"$expr": { "$eq": [ "$name", "$$varRef"] }} }] }  }], "let": { "varRef": "Dev" } }');


-- $window operators
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_setWindowFields','{ "_id": 1, "a": "abc", "cost": 10, "quantity": 501, "date": { "$date": { "$numberLong": "1718841600000" } } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_setWindowFields','{ "_id": 2, "a": "def", "cost": 8, "quantity": 502, "date": { "$date": { "$numberLong": "1718841605000" } } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_setWindowFields','{ "_id": 3, "a": "ghi", "cost": 4, "quantity": 503, "date": { "$date": { "$numberLong": "1718841610000" } } }', NULL);

SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db',
    '{ "aggregate": "aggregation_pipeline_let_setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$concat": ["$a", "$$varRef" ] }, "output": {"total": { "$sum": "$$varRefNum"}}}}], "let": { "varRef": "prefix", "varRefNum": 2 } }');

-- $lookup
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelinefrom',' {"_id": 1, "name": "American Steak House", "food": ["filet", "sirloin"], "quantity": 100 , "beverages": ["beer", "wine"]}', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelinefrom','{ "_id": 2, "name": "Honest John Pizza", "food": ["cheese pizza", "pepperoni pizza"], "quantity": 120, "beverages": ["soda"]}', NULL);

SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelineto','{ "_id": 1, "item": "filet", "restaurant_name": "American Steak House", "qval": 100 }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelineto','{ "_id": 2, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "lemonade", "qval": 120 }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelineto','{ "_id": 3, "item": "cheese pizza", "restaurant_name": "Honest John Pizza", "drink": "soda", "qval": 140 }', NULL);


SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelinefrom_second',' {"_id": 1, "country": "America", "qq": 100 }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_pipelinefrom_second','{ "_id": 2, "country": "Canada", "qq": 120 }', NULL);

-- Add a $lookup with let
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }}, { "$addFields": { "addedQval": "$$qval" }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

-- Add a $lookup with let but add a subquery stage
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$addFields": { "addedQvalBefore": "$$qval" }}, { "$sort": { "_id": 1 } }, { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }}, { "$addFields": { "addedQvalAfter": "$$qval" }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

-- nested $lookup
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$addFields": { "addedVal": "$$qval" }}, { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

-- multiple variable in lookup let

SELECT documentdb_api.insert('db', '{"insert":"orderscoll", "documents":[
  { "_id": 1, "orderId": "A001", "productId": "P001", "quantity": 10 },
  { "_id": 2, "orderId": "A002", "productId": "P002", "quantity": 5 },
  { "_id": 3, "orderId": "A003", "productId": "P001", "quantity": 2 }
]

}');

SELECT documentdb_api.insert('db', '{"insert":"products", "documents":[
  { "_id": "P001", "name": "Product 1", "price": 100 },
  { "_id": "P002", "name": "Product 2", "price": 200 }
]

}');

SELECT document from bson_aggregation_pipeline('db', '{ "aggregate": "orderscoll", "pipeline": [ { "$lookup": { "from": "products", "let": { "productId": "$productId", "hello" : "parag" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$_id", "$$productId" ] } } }, { "$project": { "name": 1, "price": 1, "_id": 0 , "field" : "$$hello" } } ], "as": "productDetails" } }, { "$unwind": "$productDetails" } ] , "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document from bson_aggregation_pipeline('db', '{ "aggregate": "orderscoll", "pipeline": [ { "$lookup": { "from": "products", "let": { "productId": "$productId", "hello" : "parag" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$_id", "$$productId" ] } } }, { "$project": { "name": 1, "price": 1, "_id": 0 , "field" : "$$hello" } } ], "as": "productDetails" } }, { "$unwind": "$productDetails" } ] , "cursor": {} }');

-- only part of the pipeline uses let
SELECT document from bson_aggregation_pipeline('db', '{ "aggregate": "orderscoll", "pipeline": [ { "$lookup": { "from": "products", "let": { "productId": "$productId", "hello" : "parag" }, "pipeline": [ { "$match": { "_id": { "$gt": "P001" } } }, { "$project": { "name": 1, "price": 1, "_id": 0 , "field" : "$$hello" } } ], "as": "productDetails" } }, { "$unwind": "$productDetails" } ] , "cursor": {} }');
EXPLAIN (COSTS OFF) SELECT document from bson_aggregation_pipeline('db', '{ "aggregate": "orderscoll", "pipeline": [ { "$lookup": { "from": "products", "let": { "productId": "$productId", "hello" : "parag" }, "pipeline": [ { "$match": { "_id": { "$gt": "P001" } } }, { "$project": { "name": 1, "price": 1, "_id": 0 , "field" : "$$hello" } } ], "as": "productDetails" } }, { "$unwind": "$productDetails" } ] , "cursor": {} }');


SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$addFields": { "addedVal": "$$qval" }}, { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }}, { "$addFields": { "addedVal": "$$qval" }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$sort": { "_id": 1 }}, { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$addFields": { "addedVal": "$$qval" }}, { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }}, { "$addFields": { "addedVal": "$$secondVar" }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');


-- with an expression that returns empty, the variable should be defined as empty
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "c": "$$c" }, "filter": {}, "let": { "c": {"$getField": {"field": "c", "input": {"a": 1}}}} }');

-- $literal should be treated as literal
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "c": "$$c" }, "filter": {}, "let": { "c": {"$literal": "$$NOW"}} }');

-- these should fail - parse and validate no path, CURRENT or ROOT in let spec.
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": "$a" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": "$$ROOT" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": "$$CURRENT" } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": {}, "let": { "varRef": { "$sum": [ "$a", "$a" ] } } }');

-- internal, lookup expression eval merge should wrap variables into $literal and if the variable evaluates to empty, should transform into $$REMOVE
SELECT documentdb_api_internal.bson_dollar_lookup_expression_eval_merge('{"_id": 1, "b": "$someField"}', '{ "local_b" : "$b", "local_a": "$a" }'::documentdb_core.bson, '{}'::documentdb_core.bson);
SELECT documentdb_api_internal.bson_dollar_lookup_expression_eval_merge('{"_id": 1, "b": "$someField"}', '{ "local_b" : "$b", "local_a": "$a", "local_var1": "$$var1" }'::documentdb_core.bson, '{"var1": {"$literal": "ABC"}}'::documentdb_core.bson);

-- lookup with let when a field is missing the path a variable references and when that field is a string in the form of a field expression

SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_lookup_missing','{"_id":"1", "a": { } }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_lookup_missing','{"_id":"2", "b": 1 }', NULL);
SELECT documentdb_api.insert_one('db','aggregation_pipeline_let_lookup_missing','{"_id":"3", "a": "$notAFieldPath" }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let_lookup_missing", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_lookup_missing", "as": "res", "let": {"local_a": "$a"}, "pipeline": [ { "$match": {"$expr": {"$eq": ["$$local_a", "$a"]}}}, {"$project":{"_id": 1}} ] } } ], "cursor": {} }');

/* Shard the collections */
SELECT documentdb_api.shard_collection('db', 'aggregation_pipeline_let', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$lt": [ "$_id", "$$varNotRef" ]} }, "let": { "varRef": "3" } }');
-- let support in $expr with nested $let
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varNotRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
-- same scenario but with aggregation pipeline
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$lt": [ "$_id", "$$varRef" ]} } } ], "let": { "varRef": "3" } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } } } ], "let": { "varRef": 3 } }');

-- let support in $expr with nested $let
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "newField" : "$$varRef" }, "filter": { "$expr": { "$let": { "vars": { "varNotRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} }, "let": { "varRef": 3 } }');
-- same scenario but with aggregation pipeline
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$match": { "$expr": { "$let": { "vars": { "varRef": 2 }, "in": { "$lt": [ { "$toInt": "$_id" }, "$$varRef" ] } }} } } ], "let": { "varRef": 3 } }');

-- find/aggregate with variables referencing other variables on the same let spec should work
SELECT document FROM bson_aggregation_find('db', '{ "find": "aggregation_pipeline_let", "projection": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply"  }, "filter": {}, "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]} } }');

-- nested $let should also work
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{"$project": { "varRef" : "$$varRef", "add": "$$add", "multiply": "$$multiply", "nestedLet": "$$nestedLet"  }}], "let": { "varRef": 20, "add": {"$add": ["$$varRef", 2]}, "multiply": {"$multiply": ["$$add", 2]}, "nestedLet": {"$let": {"vars": {"add": {"$add": ["$$multiply", 1]}}, "in": "$$add"}} } }');

-- $addFields
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "newField1" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- $replaceRoot
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$replaceRoot": { "newRoot": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } } }} ], "let": { "varRef": 3 } }');

-- $replaceWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$replaceWith": { "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- $group (with simple aggregators)
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" } } } ], "let": { "varRef": 3 } }');

-- $group (with sorted accumulators)
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" }, "first": { "$first" : "$a.b" } } } ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" }, "last": { "$last" : "$a.b" } } } ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$set": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id": "$$varRef", "top": {"$top": {"output": [ "$_id" ], "sortBy": { "_id": 1 }}}}} ], "let": { "varRef": 3 } }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$set": {"newField": "new"} }, { "$sort": { "_id": 1} }, { "$group": { "_id": "$$varRef", "bottom": {"$bottom": {"output": [ "$_id" ], "sortBy": { "_id": 1 }}}}} ], "let": { "varRef": 3 } }');

-- $project
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$project": { "_id": 1, "newField" : "$$varRef", "c": { "$lt": [ "$_id", "$$varRef" ] } }} ], "let": { "varRef": 3 } }');

-- $unionWith
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [{ "$addFields": { "c": "foo" }}, { "$unionWith": { "coll": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "bar": "$$varRef" } } ] } } ], "let": { "varRef": 30 }}');

-- $sortByCount
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$sortByCount": { "$lt": [ "$_id", "$$varRef" ] } } ], "let": { "varRef": "2" } }');

-- $facet
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let", "pipeline": [ { "$addFields": { "newField": "myvalue" }}, { "$facet": { "sb1": [ { "$addFields": { "myVar": "$$varRef" }} ], "sb2": [ { "$group": { "_id" : "$$varRef", "c": { "$sum": "$$varRef" } } } ] }} ], "let": { "varRef": "2" } }');

-- $graphLookup
SELECT documentdb_api.shard_collection('db', 'aggregation_pipeline_let_gl', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let_gl", "pipeline": [ { "$graphLookup": { "from": "aggregation_pipeline_let_gl", "startWith": { "$max": [ "$reportsTo", "$$reportsTo" ] }, "connectFromField": "reportsTo", "connectToField": "name", "as": "reportingHierarchy" } } ], "let": { "reportsTo": "Dev" } }');

-- $inverseMatch
SELECT documentdb_api.shard_collection('db', 'aggregation_pipeline_let_inv', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "aggregation_pipeline_let_inv", "pipeline": [ { "$inverseMatch": { "path": "policy", "from": "aggregation_pipeline_let_gl", "pipeline": [{"$match": {"$expr": { "$eq": [ "$name", "$$varRef"] }} }] }  }], "let": { "varRef": "Dev" } }');

-- $window operators
SELECT documentdb_api.shard_collection('db', 'aggregation_pipeline_let_setWindowFields', '{ "_id": "hashed" }', false);

SELECT document FROM bson_aggregation_pipeline('db',
    '{ "aggregate": "aggregation_pipeline_let_setWindowFields", "pipeline":  [{"$setWindowFields": {"partitionBy": { "$concat": ["$a", "$$varRef" ] }, "output": {"total": { "$sum": "$$varRefNum"}}}}], "let": { "varRef": "prefix", "varRefNum": 2 } }');

-- $lookup
SELECT documentdb_api.shard_collection('db', 'aggregation_pipeline_let_pipelineto', '{ "_id": "hashed" }', false);

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }}, { "$addFields": { "addedQval": "$$qval" }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

-- Add a $lookup with let but add a subquery stage
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$sort": { "_id": 1 } }, { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [ { "$addFields": { "addedQvalBefore": "$$qval" }}, { "$sort": { "_id": 1 } }, { "$match": { "$expr": { "$eq": [ "$quantity", "$$qval" ] } }}, { "$addFields": { "addedQvalAfter": "$$qval" }} ], "as": "matched_docs", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

-- nested $lookup
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');

SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "aggregation_pipeline_let_pipelineto", "pipeline": [ { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom", "pipeline": [{ "$addFields": { "addedVal": "$$qval" }}, { "$lookup": { "from": "aggregation_pipeline_let_pipelinefrom_second", "localField": "qq", "foreignField": "qval", "as": "myExtra", "let": { "secondVar": "$quantity" }, "pipeline": [ { "$match": { "$expr": { "$eq": [ "$qq", "$$secondVar" ] }  }} ] }} ], "as": "myMatch", "localField": "restaurant_name", "foreignField": "name", "let": { "qval": "$qval" } }} ], "cursor": {} }');


-- Test for lookup with let and $match with $expr

SELECT documentdb_api.insert_one('db','lookup_left',' { "_id" : 1, "item" : 1 }', NULL);
SELECT documentdb_api.insert_one('db','lookup_left',' { "_id" : 2, "item" : 2 }', NULL);

-- Fill right collection with more than 100 MB of data
DO $$
DECLARE i int;
BEGIN
FOR i IN 1..50 LOOP
PERFORM documentdb_api.insert_one('db', 'lookup_right', FORMAT('{ "_id": %s, "name": %s, "c": { "%s": [ %s "d" ] } }',  i, i, i, REPEAT('"' || i || REPEAT('a', 1000) || '", ', 5000))::documentdb_core.bson, NULL);
END LOOP;
END;
$$;

-- This will throw size error because $match in not inlined with right query as it is not the first stage
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$project": { "c": 0 } }, { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$project": { "c": 0 } }, { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');

-- This should not throw error as $match is inlined with right query
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');

-- Shard left collection and right collections
SET citus.explain_all_tasks to on;
SET citus.max_adaptive_executor_pool_size to 1;
SELECT documentdb_api.shard_collection('{ "shardCollection": "db.lookup_left", "key": { "_id": "hashed" }, "numInitialChunks": 2 }');
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "db.lookup_right", "key": { "_id": "hashed" }, "numInitialChunks": 2 }');
SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document from bson_aggregation_pipeline('db', 
  '{ "aggregate": "lookup_left", "pipeline": [ { "$lookup": { "from": "lookup_right", "pipeline": [ { "$match": { "$expr": { "$eq": [ "$name", "$$item_name" ] }}}, { "$project": { "c": 0 } }], "as": "myMatch", "let": { "item_name": "$item" } }} ], "cursor": {} }');
RESET citus.explain_all_tasks;
RESET citus.max_adaptive_executor_pool_size;

-- query match
-- enableLetAndCollationForQueryMatch GUC off: ignore variableSpec
SET documentdb.enableLetAndCollationForQueryMatch TO off;
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"$expr": {"a": "$$varRef"} }', '{ "let": {"varRef": "cat"} }', NULL);

-- enableLetAndCollationForQueryMatch GUC on: use variableSpec
SET documentdb.enableLetAndCollationForQueryMatch TO on;

-- query match: wrong access of variable in query (outside $expr)
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"a": "$$varRef"}', '{ "let": {"varRef": "cat"} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 5}', '{"a": {"$lt": "$$varRef"} }', '{ "let": {"varRef": 10} }', NULL);

-- query match: using $expr
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"$expr": {"$eq": ["$a", "$$varRef"] } }', '{ "let": {"varRef": "cat"} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": "cat", "b": "dog"}', '{"$expr": {"$and": [{"$eq": ["$a", "$$varRef1"] }, {"$eq": ["$b", "$$varRef2"] } ]} }', '{ "let": {"varRef1": "cat", "varRef2": "dog"} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"$expr": {"$ne": ["$a", "$$varRef"] } }', '{ "let": {"varRef": "dog"} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 2}', '{"$expr": {"$gt": ["$a", "$$varRef"] } }', '{ "let": {"varRef": 5} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 2}', '{"$expr": {"$gte": ["$a", "$$varRef"] } }', '{ "let": {"varRef": -2} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 2}', '{"$expr": {"$or": [{"$lt": ["$a", "$$varRef1"] }, {"$gt": ["$b", "$$varRef2"] } ]} }', '{ "let": {"varRef1": 2, "varRef2": 2} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 2}', '{"$expr": {"$and": [{"$lt": ["$a", "$$varRef1"] }, {"$gt": ["$b", "$$varRef2"] } ]} }', '{ "let": {"varRef1": 2, "varRef2": 2} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": 2}', '{"$expr": {"$not": {"$lt": ["$a", "$$varRef"] } } }', '{ "let": {"varRef": 5} }', NULL);
SELECT documentdb_api_internal.bson_query_match('{"a": "cat"}', '{"$expr": {"$in": ["$a", ["$$varRef1", "$$varRef2"]] } }', '{ "let": {"varRef1": "cat", "varRef2": "dog"} }', NULL);

-- query match: sharded collection
SELECT documentdb_api.shard_collection('db', 'coll_query_op_let', '{ "_id": "hashed" }', false);
SELECT documentdb_api.insert_one('db', 'coll_query_op_let', '{"_id": 1, "a": "cat" }', NULL);
SELECT documentdb_api.insert_one('db', 'coll_query_op_let', '{"_id": 2, "a": "dog" }', NULL);

SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE documentdb_api_internal.bson_query_match(document, '{"$expr": {"$eq": ["$a", "$$varRef"] } }', '{ "let": {"varRef": "cat"} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$ne": ["$a", "$$varRef"] } }', '{ "let": {"varRef": "dog"} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$in": ["$a", ["$$varRef1", "$$varRef2"]] } }', '{ "let": {"varRef1": "cat", "varRef2": "dog"} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$lt": ["$_id", "$$varRef"] } }', '{ "let": {"varRef": 2} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$lte": ["$_id", "$$varRef"] } }', '{ "let": {"varRef": 2} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$gt": ["$_id", "$$varRef"] } }', '{ "let": {"varRef": 1} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$gte": ["$_id", "$$varRef"] } }', '{ "let": {"varRef": 1} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$and": [{"$lt": ["$_id", "$$varRef1"] }, {"$gt": ["$_id", "$$varRef2"] } ]} }', '{ "let": {"varRef1": 2, "varRef2": 1} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$or": [{"$lt": ["$_id", "$$varRef1"] }, {"$gt": ["$_id", "$$varRef2"] } ]} }', '{ "let": {"varRef1": 2, "varRef2": 1} }', NULL);
SELECT document from documentdb_api.collection('db', 'coll_query_op_let') WHERE  documentdb_api_internal.bson_query_match(document, '{"$expr": {"$not": {"$lt": ["$_id", "$$varRef"] } } }', '{ "let": {"varRef": 1} }', NULL);

RESET documentdb.enableLetAndCollationForQueryMatch;


-- let with geonear
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "agg_geonear_let", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_ab_idx" }, {"key": {"a.b": "2dsphere"}, "name": "my_2ds_ab_idx" }]}');
SELECT documentdb_api.insert_one('db','agg_geonear_let','{ "_id": 1, "a": { "b": [ 5, 5]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear_let','{ "_id": 2, "a": { "b": [ 5, 6]} }', NULL);
SELECT documentdb_api.insert_one('db','agg_geonear_let','{ "_id": 3, "a": { "b": [ 5, 7]} }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', $$
{
    "aggregate": "agg_geonear_let", 
    "pipeline": [
      {
        "$geoNear":
          { 
            "near": "$location", 
            "distanceField": "dist.calculated",
            "key": "a.b" 
          } 
      } 
    ]
}
$$);

SELECT document FROM bson_aggregation_pipeline('db', $$
{
    "aggregate": "agg_geonear_let", 
    "pipeline": [
      {
        "$geoNear":
          { 
            "near": { "$literal" : "Not a valid point" }, 
            "distanceField": "dist.calculated",
            "key": "a.b" 
          } 
      } 
    ],
    "let": { "pointRef": [5, 6], "dist1": 0, "dist2": 2}
}
$$);

SELECT document FROM bson_aggregation_pipeline('db', $$
{
    "aggregate": "agg_geonear_let", 
    "pipeline": [
      {
        "$geoNear":
          { 
            "near": { "$literal" : [5, 6] }, 
            "distanceField": "dist.calculated",
            "minDistance": "$p",
            "key": "a.b" 
          } 
      } 
    ],
    "let": { "pointRef": [5, 6], "dist1": 0, "dist2": 2}
}
$$);

SELECT document FROM bson_aggregation_pipeline('db', $$ 
{
    "aggregate": "agg_geonear_let", 
    "pipeline": [
      {
        "$geoNear":
          { 
            "near": { "$literal" : [5, 6] }, 
            "distanceField": "dist.calculated",
            "maxDistance": { "$literal": [1, 2] },
            "key": "a.b" 
          } 
      } 
    ],
    "let": { "pointRef": [5, 6], "dist1": 0, "dist2": 2}
}
$$);

-- a valid query
SELECT document FROM bson_aggregation_pipeline('db', $spec$
{
    "aggregate": "agg_geonear_let", 
    "pipeline": [
      {
        "$geoNear":
          { 
            "near": "$$pointRef", 
            "distanceField": "dist.calculated",
            "minDistance": "$$dist1",
            "maxDistance": "$$dist2",
            "key": "a.b" 
          } 
      } 
    ],
    "let": { "pointRef": [5, 5], "dist1": 0, "dist2": 2}
}
$spec$);