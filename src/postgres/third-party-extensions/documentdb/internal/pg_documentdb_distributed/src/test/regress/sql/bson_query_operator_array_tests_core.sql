
-- some documents with mixed types
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 1, "value": 42 }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 2, "value": -999 }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 3, "value": { "longNum": "2048" } }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 4, "value": false }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 5, "value": "alpha beta" }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 6, "value": { "subfield": 7 } }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 7, "value": { "dateField": { "longNum": "654321" } } }');

-- now insert some documents with arrays with those terms
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 100, "value": [ 42, "bravo charlie", false ] }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 101, "value": [ false, -999, { "subfield": 7 }, 8, 9, 10 ] }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 102, "value": [ false, -999, { "subfield": 7 }, 42, 99, { "dateField": { "longNum": "654321" } } ] }');

-- now insert some documents with arrays of arrays of those terms
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 200, "value": [ 42, [ false, "alpha beta" ] ] }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 201, "value": [ false, -999, { "subfield": 7 }, [ 42, "bravo charlie", false ] ] }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 202, "value": [ [ false, -999, { "subfield": 7 }, 8, 9, 10 ] ] }');

-- insert empty arrays
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 300, "value": [ ] }');
SELECT documentdb_api.insert_one('array_query_db', 'array_operator_tests', '{ "_id": 301, "value": [ [], "zuluValue" ] }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$eq": [ 42, "bravo charlie", false ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$gt": [ 42, "bravo charlie", false ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$gte": [ 42, "bravo charlie", false ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$gt": [ false, -999, { "subfield": 7 }, 42, 99, { "dateField": { "longNum": "654321" } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$gte": [ false, -999, { "subfield": 7 }, 42, 99, { "dateField": { "longNum": "654321" } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$lt": [ true, -999, { "subfield": 7 }, 42, 99, { "dateField": { "longNum": "654321" } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$lte": [ true, -999, { "subfield": 7 }, 42, 99, { "dateField": { "longNum": "654321" } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$eq": [ ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$in": [ [ 42, "bravo charlie", false ], [ 42, [ false, "alpha beta" ] ] ]} } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$in": [ [ 42, "bravo charlie", false ], [ ] ]} } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$size": 3 } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$size": 2 } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$size": 0 } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$all": [ 42, false ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$all": [ 42 ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$all": [ { "$elemMatch": { "$gt": 0 } }, { "$elemMatch": { "subfield": 7 } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$all": [ { "$elemMatch": { "$gt": 8, "$lt": 10 } }, { "$elemMatch": { "subfield": 7 } } ] } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$elemMatch": { "$gt": 8, "$lt": 10 } } } }');

SELECT document FROM bson_aggregation_find('array_query_db', '{ "find": "array_operator_tests", "filter": { "value": { "$elemMatch": { "subfield": { "$gt": 0 } } } } }');