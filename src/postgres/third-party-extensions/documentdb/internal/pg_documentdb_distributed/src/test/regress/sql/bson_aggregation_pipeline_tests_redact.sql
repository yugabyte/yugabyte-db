SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SET citus.next_shard_id TO 5150000;
SET documentdb.next_collection_id TO 51500;
SET documentdb.next_collection_index_id TO 51500;

/* Insert data, normal case*/
SELECT documentdb_api.insert_one('db','redactTest1','{ "_id": 1, "level": "public", "content": "content 1", "details": { "level": "public", "value": "content 1.1", "moreDetails": { "level": "restricted", "info": "content 1.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest1','{ "_id": 2, "level": "restricted", "content": "content 2", "details": { "level": "public", "value": "content 2.1", "moreDetails": { "level": "restricted", "info": "content 2.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest1','{ "_id": 3, "level": "public", "content": "content 3", "details": { "level": "restricted", "value": "content 3.1", "moreDetails": { "level": "public", "info": "content 3.1.1" } } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest1','{ "_id": 4, "content": "content 4", "details": { "level": "public", "value": "content 4.1" } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest1','{ "_id": 5, "level": "public", "content": "content 5", "details": { "level": "public", "value": "content 5.1", "moreDetails": [{ "level": "restricted", "info": "content 5.1.1" }, { "level": "public", "info": "content 5.1.2" }] } }', NULL);


SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "redactTest1", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "public"] }, "then": "$$KEEP", "else": "$$PRUNE" } } }  ], "cursor": {} }');

SELECT document FROM bson_aggregation_pipeline('db', 
    '{ "aggregate": "redactTest1", "pipeline": [ { "$redact": { "$cond": { "if": { "$eq": ["$level", "public"] }, "then": "$$DESCEND", "else": "$$PRUNE" } } }  ], "cursor": {} }');

/* Insert data, add validation for array-nested subdocument, array of documents, array of arrays, empty document being returned,  test DESCEND and PRUNE*/
SELECT documentdb_api.insert_one('db','redactTest2','{ "_id": 1, "level": 1, "a": { "level": 2, "b": { "level": 4, "c": { "level": 1, "d": 8 } } }, "e": 20 }', NULL);
SELECT documentdb_api.insert_one('db','redactTest2','{ "_id": 2, "level": 4, "a": { "level": 3, "b": [{ "level": 1, "e": 4 }] } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest2','{ "_id": 3, "level": 2, "a": { "b": { "level": 3, "n": 12 } } }', NULL);
SELECT documentdb_api.insert_one('db','redactTest2','{ "_id": 4, "level": 3, "a": [{ "level": 5, "b": 20 }] }', NULL);
SELECT documentdb_api.insert_one('db','redactTest2','{ "_id": 5, "level": 3, "b": { "level": 3, "d": [ { "level": 1, "e": 4 }, { "level": 5, "g": 9 }, [{ "level": 1, "r": 11 }, 52, 42, 75, { "level": 5, "s": 3 }]] } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 1]}, "$$DESCEND", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 2]}, "$$DESCEND", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 3]}, "$$DESCEND", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 4]}, "$$DESCEND", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 5]}, "$$DESCEND", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest2", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 5]}, { "$literal" :"$$DESCEND"}, "$$PRUNE"] } }  ], "cursor": {} }');

/* Insert data, test KEEP*/
SELECT documentdb_api.insert_one('db','redactTest3','{ "_id": 1, "level": 1, "a": { "level": 1, "b": 1 }, "c": { "level": 1, "d": 1 }}', NULL);
SELECT documentdb_api.insert_one('db','redactTest3','{ "_id": 2, "level": 2, "a": { "level": 2, "b": 2 }, "c": { "level": 2, "d": 2 }}', NULL);
SELECT documentdb_api.insert_one('db','redactTest3','{ "_id": 3, "level": 3, "a": { "level": 3, "b": 3 }, "c": { "level": 3, "d": 3 }}', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 1]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 2]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 3]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');

/*test directly systemvariable*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$KEEP" }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$PRUNE"}], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$DESCEND"}], "cursor": {} }');

/*switch case*/
SELECT documentdb_api.insert_one('db','redactTestSwitch','{ "_id": 1, "title": "Document 1", "classification": "confidential", "content": "This is confidential content.", "metadata": { "author": "Alice", "revision": 3 } }', NULL);
SELECT documentdb_api.insert_one('db','redactTestSwitch','{ "_id": 2, "title": "Document 2", "classification": "public", "content": "This is public content.", "metadata": { "author": "Bob", "revision": 1 } }', NULL);
SELECT documentdb_api.insert_one('db','redactTestSwitch','{ "_id": 3, "title": "Document 3", "classification": "restricted", "content": "This content is restricted.", "metadata": { "author": "Charlie", "revision": 2 } }', NULL);

SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTestSwitch", "pipeline": [ { "$redact": { "$switch": { "branches": [ { "case": { "$eq": ["$classification", "confidential"] }, "then": "$$PRUNE" }, { "case": { "$eq": ["$classification", "restricted"] }, "then": { "$cond": { "if": { "$eq": ["$content", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } }], "default": "$$KEEP" } }  }], "cursor": {} }');

/*$lookup $let and $redact example*/
SELECT documentdb_api.insert_one('db','redactUsers','{ "_id": 1, "name": "Alice"}', NULL);
SELECT documentdb_api.insert_one('db','redactUsers','{ "_id": 2, "name": "Bob"}', NULL);
SELECT documentdb_api.insert_one('db','redactUsers','{ "_id": 3, "name": "Charlie"}', NULL);

SELECT documentdb_api.insert_one('db','redactOrders','{ "_id": 1, "user_id": 1, "order_amount":150, "status": "completed"}', NULL);
SELECT documentdb_api.insert_one('db','redactOrders','{ "_id": 2, "user_id": 1, "order_amount":200, "status": "pending"}', NULL);
SELECT documentdb_api.insert_one('db','redactOrders','{ "_id": 3, "user_id": 2, "order_amount":500, "status": "completed"}', NULL);
SELECT documentdb_api.insert_one('db','redactOrders','{ "_id": 4, "user_id": 2, "order_amount":50, "status": "completed"}', NULL);
SELECT documentdb_api.insert_one('db','redactOrders','{ "_id": 5, "user_id": 3, "order_amount":300, "status": "pending"}', NULL);

/* $redact with $let*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactUsers", "pipeline": [ { "$lookup": { "from": "redactOrders", "localField": "_id", "foreignField": "user_id", "as": "user_orders", "pipeline": [ { "$redact": { "$cond": { "if": { "$let": { "vars": { "totalAmount": { "$sum": "$$ROOT.order_amount" } }, "in": { "$gt": [ "$$totalAmount", 40 ] } } }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');
/* add $let with the same level with $lookup, different variable with $let in $redact*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactUsers", "pipeline": [ { "$lookup": { "from": "redactOrders", "localField": "_id", "foreignField": "user_id", "as": "user_orders", "let": { "user_id": "$_id" }, "pipeline": [ { "$redact": { "$cond": { "if": { "$let": { "vars": { "totalAmount": { "$sum": "$order_amount" } }, "in": { "$gt": [ "$$totalAmount", 40 ] } } }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');
/* two level $let has the same variable*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactUsers", "pipeline": [ { "$lookup": { "from": "redactOrders", "localField": "_id", "foreignField": "user_id", "as": "user_orders", "let": { "user_id": "$_id", "totalAmount": { "$sum": "$user_orders.order_amount" } }, "pipeline": [ { "$redact": { "$cond": { "if": { "$let": { "vars": { "totalAmount": { "$sum": "$order_amount" } }, "in": { "$gt": [ "$$totalAmount", 40 ] } } }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');
/* redact use top level $let*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactUsers", "pipeline": [ { "$lookup": { "from": "redactOrders", "localField": "_id", "foreignField": "user_id", "as": "user_orders", "let": { "user_id": "$_id" }, "pipeline": [ { "$redact": { "$cond": { "if": { "$let": { "vars": { "totalAmount": { "$sum": "$order_amount" } }, "in": { "$and": [ { "$gt": [ "$$totalAmount", 40 ] }, { "$lt": [ "$$user_id", 2 ] } ] } } }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');

/*facet with redact test case user data*/
SELECT documentdb_api.insert_one('db', 'facetUsers', '{ "_id": 1, "name": "Alice", "email": "alice@example.com", "age": 25 }', NULL);
SELECT documentdb_api.insert_one('db', 'facetUsers', '{ "_id": 2, "name": "Bob", "email": "bob@example.com", "age": 25 }', NULL);
SELECT documentdb_api.insert_one('db', 'facetUsers', '{ "_id": 3, "name": "Charlie", "email": "charlie@example.com", "age": 30 }', NULL);
/*facet with redact case, one is empty array and the other is not*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "facetUsers", "pipeline": [ { "$facet": { "ageGroup": [ { "$group": { "_id": "$age", "users": { "$push": "$name" } } } ], "redactEmail": [ { "$redact": { "$cond": { "if": { "$eq": ["$email", null] }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "facetUsers", "pipeline": [ { "$facet": { "ageGroup": [ { "$group": { "_id": "$age", "users": { "$push": "$name" } } } ], "redactEmail": [ { "$redact": { "$cond": { "if": { "$eq": ["$email", "alice@example.com"] }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');

/*graphLookup with redact test case employee data*/
SELECT documentdb_api.insert_one('db', 'graphLookupEmployee', '{ "_id": 1, "employee_id": 1, "name": "Alice", "manager_id": null, "email": "alice@example.com" }', NULL);
SELECT documentdb_api.insert_one('db', 'graphLookupEmployee', '{ "_id": 2, "employee_id": 2, "name": "Bob", "manager_id": 1, "email": "bob@example.com" }', NULL);
SELECT documentdb_api.insert_one('db', 'graphLookupEmployee', '{ "_id": 3, "employee_id": 3, "name": "Charlie", "manager_id": 1, "email": "charlie@example.com" }', NULL);
SELECT documentdb_api.insert_one('db', 'graphLookupEmployee', '{ "_id": 4, "employee_id": 4, "name": "David", "manager_id": 2, "email": "david@example.com" }', NULL);
SELECT documentdb_api.insert_one('db', 'graphLookupEmployee', '{ "_id": 5, "employee_id": 5, "name": "Eve", "manager_id": 2, "email": "eve@example.com" }', NULL);
/*graphLookup with redact test case, prune employee do not have subordinates*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "graphLookupEmployee", "pipeline": [ { "$graphLookup": { "from": "graphLookupEmployee", "startWith": "$employee_id", "connectFromField": "employee_id", "connectToField": "manager_id", "as": "subordinates" } }, { "$redact": { "$cond": { "if": { "$eq": [{ "$size": "$subordinates" }, 0] }, "then": "$$PRUNE", "else": "$$KEEP" } } } ], "cursor": {} }');

/*unionWith with redact test case user and employee data*/
SELECT documentdb_api.insert_one('db', 'unionWithUsers', '{ "_id": 1, "name": "Alice", "role": "admin", "address": { "city": "New York", "zip": "10001" } }', NULL);
SELECT documentdb_api.insert_one('db', 'unionWithUsers', '{ "_id": 2, "name": "Bob", "role": "user", "address": { "city": "Los Angeles", "zip": "90001" } }', NULL);
SELECT documentdb_api.insert_one('db', 'unionWithEmployees', '{ "_id": 101, "name": "Charlie", "role": "admin", "department": "HR", "address": { "city": "San Francisco", "zip": "94101" } }', NULL);
SELECT documentdb_api.insert_one('db', 'unionWithEmployees', '{ "_id": 102, "name": "David", "role": "user", "department": "IT", "address": { "city": "Chicago", "zip": "60601" } }', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "unionWithUsers", "pipeline": [ { "$unionWith": { "coll": "unionWithEmployees", "pipeline": [ { "$redact": { "$cond": { "if": { "$gt": ["$_id", 101] }, "then": "$$KEEP", "else": "$$PRUNE" } } } ] } } ], "cursor": {} }');

/*negative case*/
/*redact input as number*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": 1 }], "cursor": {} }');
/*redact input as string*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "string" }], "cursor": {} }');
/*redact input as boolean*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": true }], "cursor": {} }');
/*redact input system variable not KEEP or PRUNE or DESCEND*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$NOTVALID"}], "cursor": {} }');
/*redact input as array*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": ["$$KEEP", "$$PRUNE"]} ], "cursor": {} }');
/*redact input cond return not KEEP or PRUNE or DESCEND*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": {"if": {"$eq": ["$level", 1]}, "then": "$$KEEP", "else": "$$NOTVALID"} } } ], "cursor": {} }');
/*throw exception when $$KEEP $$PRUEN $$DESCEND are used in other stages*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$project": {"filteredField" : "$$PRUNE"} } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$project": {"newField" : "$$DESCEND"} } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$project": {"newField" :{ "$add":["$$KEEP",1]} } } ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$addFields": {"newField" : "$$DESCEND"} } ], "cursor": {} }');

/*EXPLAIN*/
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$KEEP" } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": {"if": {"$eq": ["$level", 1]}, "then": "$$KEEP", "else": "$$PRUNE"} } } ] }');

/*sharded collection*/
SELECT shard_collection('db', 'redactTest3', '{ "amount": "hashed" }', false);
/*positive cases:*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 1]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 2]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": [{"$lte": ["$level", 3]}, "$$KEEP", "$$PRUNE"] } }  ], "cursor": {} }');

/*test directly systemvariable*/
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$KEEP" }], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$PRUNE"}], "cursor": {} }');
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$DESCEND"}], "cursor": {} }');

/*EXPLAIN*/
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": "$$KEEP" } ] }');
EXPLAIN (VERBOSE ON, COSTS OFF) SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', '{ "aggregate": "redactTest3", "pipeline": [ { "$redact": { "$cond": {"if": {"$eq": ["$level", 1]}, "then": "$$KEEP", "else": "$$PRUNE"} } } ] }');
