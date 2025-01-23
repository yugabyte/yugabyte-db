SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 6900000;
SET documentdb.next_collection_id TO 6900;
SET documentdb.next_collection_index_id TO 6900;

-- $in operator
-- returns expected result
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [1, [1, 2]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$in": ["$a", [1, { }]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$in": ["$a", [1, { "a": true}]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": true}}', '{"result": { "$in": ["$a", [1, { "b": true}]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": true}}', '{"result": { "$in": ["$a.b", [1, true]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": true}}', '{"result": { "$in": ["$a.b", [null, false]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": { "$in": ["$a.b", [1, [1, 2], 2]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": { "$in": ["$a.b", [1, [1, 3], 2]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": true}}', '{"result": { "$in": ["$a.b", []]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [{"$numberDouble": "NaN"}, [1, {"$numberDouble": "NaN"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [{"$numberDouble": "NaN"}, [1, {"$numberDouble": "0.0"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [{"$numberDouble": "NaN"}, [1, {"$numberDouble": "Infinity"}]]}}');

-- null should match mising paths and null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [null, ["$a", 2]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [null, [1, 2, 3, null]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": null}}', '{"result": { "$in": [null, [1, 2, 3, "$a.b"]]}}');

-- undefined expression as first shouldn't match null/undefined in array
SELECT * FROM bson_dollar_project('{"a": {"b": null}}', '{"result": { "$in": ["$z", [1, 2, 3, "$a.b"]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": null}}', '{"result": { "$in": ["$z", [null]]}}');

-- nested expressions in array should be evaluated
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, [1, 2, {"$add": [1, 1, 1]}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, [1, 2, {"$add": [1, 1, 3]}]]}}');
SELECT * FROM bson_dollar_project('{"a": true, "b": [1, 2]}', '{"result": { "$in": ["$a", [1, 2, {"$isArray": "$b"}]]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$in": [{"$literal": "$a"}, [1, 2, {"$literal": "$a"}]]}}');

-- second argument must be an array
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, "$b"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, {"$undefined": true}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, { }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3, "str"]}}');
SELECT * FROM bson_dollar_project('{"a": true}', '{"result": { "$in": [3, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$in": [3, "$a"]}}');

-- number of arguments should be 2
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": 2}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [1, 2, 3, 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [1, 2, 3, 4, 6, 7, 8]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$in": [{"$not": []}, 2, 3]}}');

-- $size operator
-- returns expected result
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [[1, 2]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [[1, [1,2]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": {"$literal": [1]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [[]]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$size": ["$a"]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$size": "$a"}}');

-- should error if no array is passed in
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": "$b"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [{ }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [true]}}');

-- should error if wrong number of args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [null, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [{"$divide": [1, 0]}, 2]}}');

-- should honor nested expression errors
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [{"$divide": [1, 0]}]}}');

-- nested mongo evaluates expressions in array eventhough they are not needed for $size calculation
-- for error validation. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$size": [[{"$divide": [1, 0]}, 2]]}}');

-- $arrayElemAt
-- returns expected element positive idx
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1], 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberLong": "1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDecimal": "1.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDouble": "1.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 3]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$arrayElemAt": ["$a", 1]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 100, 3, 4]}', '{"result": { "$arrayElemAt": [[{"$add": [{"$arrayElemAt": ["$a", 0]}, {"$arrayElemAt": ["$a", 1]}]}], 0]}}');

-- returns expected element negative idx
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1], -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -4]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$arrayElemAt": ["$a", -4]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 100, 3, 4]}', '{"result": { "$arrayElemAt": [[{"$add": [{"$arrayElemAt": ["$a", -4]}, {"$arrayElemAt": ["$a", -3]}]}], -1]}}');

-- returns no result idx out of bounds
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1], -2]}, "otherResult": {"$add": [1,2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -20]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -30]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], -40]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$arrayElemAt": ["$a", -60]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 100, 3, 4]}', '{"result": { "$arrayElemAt": [[{"$add": [{"$arrayElemAt": ["$a", -4]}, {"$arrayElemAt": ["$a", -3]}]}], -70]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1], 1]}, "otherResult": {"$add": [1,2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 6]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], 7]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4]}', '{"result": { "$arrayElemAt": ["$a", 8]}}');
SELECT * FROM bson_dollar_project('{"a": [1, 100, 3, 4]}', '{"result": { "$arrayElemAt": [[{"$add": [{"$arrayElemAt": ["$a", 0]}, {"$arrayElemAt": ["$a", 1]}]}], 9]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDouble": "2147483647.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDouble": "-2147483648.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDecimal": "2147483647.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDecimal": "-2147483648.0"}]}}');

-- should return null if array is null or undefined
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [null, -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": ["$a", -5]}}');

-- should error if first arg is not array
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [{}, -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": ["string", -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [true, -5]}}');

-- should error if second arg is not a number
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], [1, 2]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], "string"]}}');

-- should return null if second arg is null or undefined
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], "$a"]}}');

-- should error if second arg is not representable as a 32-bit integer
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberLong": "2147483648"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberLong": "-2147483649"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberDouble": "1.1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberDouble": "1.0003"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDouble": "2147483648.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, 2, 3, {"a": 5}], {"$numberDouble": "-2147483649.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberDecimal": "2147483648.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberDecimal": "-2147483649.0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7], {"$numberDecimal": "1.0003"}]}}');

-- should honor nested expression errors even second arg is not a 32-bit integer or if the value is found (even if the index points to an expression that doesn't produce an error).
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[{"$divide": [1, 0]}], {"$numberDecimal": "1.0003"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[1, {"$divide": [1, 0]}], 0]}}');

-- should error if wrong number of args is provided
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [7]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayElemAt": [[7, {"$divide": [1, 0]}], {"$numberLong": "2147483648"}, 2]}}');

-- $first operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [[1]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [[1, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [[{"$add": [2, 4, 6]}, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{"a": [100, 2, 3, 4]}', '{"result": { "$first": "$a"}}');

-- null or undefined array should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": ["$a"]}}');

-- should error if no array is provided
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [{}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": ["string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [1]}}');

-- should error with wrong number of args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [[], 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [{"$divide": [1, 0]}, 2]}}');

-- should honor nested expression errors
SELECT * FROM bson_dollar_project('{}', '{"result": { "$first": [[1, 2, 3, 4, {"$divide": [1, 0]}]]}}');

-- $last operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [[1]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [[1, 2, 3, 4]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [[2, 3, 4, {"$add": [2, 4, 6]}]]}}');
SELECT * FROM bson_dollar_project('{"a": [100, 2, 3, 4]}', '{"result": { "$last": "$a"}}');

-- null or undefined array should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": ["$a"]}}');

-- should error if no array is provided
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [{}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": ["string"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [1]}}');

-- should error with wrong number of args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [[], 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [{"$divide": [1, 0]}, 2]}}');

-- should honor nested expression errors
SELECT * FROM bson_dollar_project('{}', '{"result": { "$last": [[{"$divide": [1, 0]}, 1, 2, 3, 4]]}}');

-- $objectToArray: returns expected array
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": "$b" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": 1 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": [{ "a": 1 }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": 1, "b": 2 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": [1, {"b": 2}] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": {"b": { "c": {"d": "hello"}}}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": 1, "b": {"$add": [1, 1]}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": { "a": 1, "b": {"c": { "d": { "$add": [1, 1]}}}}}}');
SELECT * FROM bson_dollar_project('{"a": {"b": {"c": 2}}}', '{"result": { "$objectToArray": { "a": "$a", "b": "$a.b", "c": "$a.b.c" }}}');
SELECT * FROM bson_dollar_project('{"a": {"b": {"c": 2}}}', '{"result": { "$objectToArray": "$a"}}');

-- $objectToArray: null or undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$objectToArray": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$objectToArray": {"$arrayElemAt": [[1], 5]}}}');

-- $objectToArray: expects document as input
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": "string"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": false}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": {"$add": [1, 2]}}}');

-- $objectToArray: accepts exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$objectToArray": [{}, {}]}}');

-- $arrayToObject: returns expected object
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", 1], ["b", {"$add": [1, 1]}]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a.b.c", true]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a.b.c", [1, 2, 3]]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": { "$literal": [[ "key", "value"], [ "qty", 25 ]]}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "value"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": 1}, {"k": "b", "v": {"$add": [1, 1]}}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a.b.c", "v": true}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a.b.c", "v": [1, 2, 3]}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": { "$literal": [{ "k": "item", "v": "abc123"}, { "k": "qty", "v": 25 }]}}}');

-- $arrayToObject: deduplicates if multiple keys are the same
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"], ["a", "value2"], ["a", "final value"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"], ["b", "value2"], ["a", "final value"], ["b", true], ["c", 1], ["c", 2]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "value"}, {"k": "b", "v": "value2"}, {"k": "c", "v": "value3"}, {"k": "c", "v": "final"}, {"k": "b", "v": "final"}, {"k": "a", "v": "final"}]]}}');

-- $arrayToObject: null or undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": ["$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [{"$objectToArray": [null]}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [{"$objectToArray": {"$arrayElemAt": [[1], -2]}}]}}');

-- $objectToArray -> $arrayToObject roundtrips
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayToObject": { "$objectToArray": { "a": 1 }}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayToObject": { "$objectToArray": { "a": 1, "b": {"$add": [1, 1]}}}}}');

-- $arrayToObject: accepts exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [1, 2]}}');

-- $arrayToObject: input must be array
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [{}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [{"$add": [1, 1]}]}}');

-- $arrayToObject: requires consistent input all objects or all arrays
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"], {"k": "b", "v": 1}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "b", "v": 1}, ["a", "value"]]]}}');

-- $arrayToObject: key must be string
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[[1, "value"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"], ["b", "foo"], [2, 2]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": 1, "v": "value"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "value"}, {"k": "b", "v": "value"}, {"k": 1, "v": "value"}]]}}');

-- $arrayToObject: array input format must be of length 2
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["value"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["a", "value"], ["b", "v"], [1, 2, 3]]]}}');

-- $arrayToObject: document input format must have 2 fields
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "b"}, {"k": "a"}]]}}');

-- $arrayToObject: document input must have a 'k' and a 'v' field
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "foo": "value"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "b"}, {"v": "a", "blah": "k"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "a", "v": "b"}, {"v": "a", "k": "k"}, {"blah": 1, "foo": 2}]]}}');

-- $arrayToObject: key must not contain embedded null byte
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "fo\u0000os", "v": "value"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[{"k": "fo\u0000", "v": "value"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["\u0000", "value"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$arrayToObject": [[["\u0000hello", "value"]]]}}');

--$slice Operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$slice": [[1,2,3],1,1]}}');
SELECT * FROM bson_dollar_project('{"a": [1,2,3]}', '{"result": { "$slice": ["$a",1,1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$slice": [[1,2,{"$add":[1,2,3]}],{"$add":[0,1]},1]}}');
SELECT * FROM bson_dollar_project('{"a": [1,2,3]}', '{"result": { "$slice": ["$b",1,1]}}');

--$slice with positive array field input
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : [[1,2,3,4,5],1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",10,10]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",2,10]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",10,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",10]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$b",10]} }');

--$slice with Negative array field input or Zero
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : [[1,2,3,4,5],-1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : [[1,2,3,4,5],-2]} }');
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : ["$a",-1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : [[1,2,3,4,5],1,0]} }');
select bson_dollar_project('{"_id":"1", "a" : 1  }', '{"slicedArray" : { "$slice" : [[1,2,3,4,5],2,-1]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$b",-1,2]} }');

--$slice with other expression
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : [[{"$add": [1,2,3]},2,3,4,5],1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : [[{"$add": [1,2,3]},2,3,4,5],{"$multiply":[1,2]},3]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : ["$a",-2,2]} }');

--$slice with inclusion or exclusion spec
select bson_dollar_project('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : ["$a",1,2]} , "b":1, "d":1}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : ["$a",1,2]} , "b":0}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : ["$a",1,2]} , "b":0, "d":1}');

--$slice has NULL input
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : [null,1,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : [[1,2,3],null,2]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"a" : { "$slice" : [[1,2,3],1,null]} }');

--Invalid Input Negative cases $slice projection
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : "str"}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["hello",1,2]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a","str",2]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",1,"str"]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",1,1.02]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",1,-1]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",1,0]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a"]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",1,2,4]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",{ "$numberDecimal" : "NaN"},{ "$numberDecimal" : "NaN"}]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",{ "$numberDecimal" : "NaN"},{ "$numberDecimal" : "Infinity"}]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",{ "$numberDecimal" : "Infinity"},{ "$numberDecimal" : "NaN"}]}}');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : ["$a",{ "$numberDecimal" : "Infinity"},{ "$numberDecimal" : "Infinity"}]}}');

-- $concatArrays operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": []}}');
SELECT * FROM bson_dollar_project('{"array": []}', '{"result": { "$concatArrays": [[], "$array"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4", "2"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4", "2", ["nested", "array"]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4", "2", ["nested", "array", ["more", "nested", "array"]]]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [[{"$add": [1, 1]}, [{"$add": [1,2]}]]]}}');
SELECT * FROM bson_dollar_project('{"a": {"b": ["1","2"], "c": ["3", "4"]}}', '{"result": { "$concatArrays": ["$a.b", "$a.c"]}}');

-- $concatArrays the indexes are preserved and we can query the result via array index keys.
WITH r1 AS (SELECT bson_dollar_project('{"doc": {"foo": "value"}, "array": ["first", "second"]}', '{"result": {"$concatArrays": [["$doc"], "$array"]}}') as result) SELECT * FROM r1 WHERE result @= '{"result.2": "second"}';
WITH r1 AS (SELECT bson_dollar_project('{"doc": {"foo": "value"}, "array": ["first", "second"]}', '{"result": {"$concatArrays": [["$doc"], "$array"]}}') as result) SELECT * FROM r1 WHERE result @= '{"result.0": {"foo": "value"}}';
WITH r1 AS (SELECT bson_dollar_project('{}', '{"result": {"$concatArrays": [["0", "1"], ["2", "3", "4", "5"]]}}') as result) SELECT * FROM r1 WHERE result @= '{"result.0": "0"}';
WITH r1 AS (SELECT bson_dollar_project('{}', '{"result": {"$concatArrays": [["0", "1"], ["2", "3", "4", "5"]]}}') as result) SELECT * FROM r1 WHERE result @= '{"result.4": "4"}';

-- $concatArrays null/undefined argument should result in null. Nested nulls, should just be nulls in the final array
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": ["$undef"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": "$undef"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], null, ["3", "4"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4"], null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4", null]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], ["3", "4", "$undef"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [["1","2"], "$undef", ["3", "4", "$undef"]]}}');
SELECT * FROM bson_dollar_project('{"doc": {"foo": "string"}, "array": ["first", "second", null]}', '{"result": { "$concatArrays": [["$doc"], "$array"]}}');

-- $concatArrays error when arguments are not an array
SELECT * FROM bson_dollar_project('{"a": {"b": "1"}}', '{"result": { "$concatArrays": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": {"b": "1"}}', '{"result": { "$concatArrays": ["$a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [[1], {"a": "1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": [true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$concatArrays": ["string"]}}');

-- $filter simple cond
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": false}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": null}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": 1}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": 0}} }');

-- $filter use cond with expression and reference variable for each element
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "cond": {"$gt": ["$$this", 3]}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "cond": {"$lt": ["$$this", 4]}}} }');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": {"$lt": ["$$this", 4]}}} }');
SELECT * FROM bson_dollar_project('{"a": {"$undefined": true}}', '{"result": {"$filter": {"input": "$a", "cond": {"$lt": ["$$this", 4]}}} }');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": {"$filter": {"input": "$a", "cond": {"$lt": ["$$this", 4]}}} }');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": {"$filter": {"input": "$undefinedPath", "cond": {"$lt": ["$$this", 4]}}} }');
SELECT * FROM bson_dollar_project('{"a": ["robert", "john", null, "david", null, null, "eric"]}', '{"result": {"$filter": {"input": "$a", "cond": {"$ne": ["$$this", null]}}} }');
SELECT * FROM bson_dollar_project('{"a": ["robert", "john", null, "david", null, null, "eric"]}', '{"result": {"$filter": {"input": "$a", "cond": {"$ne": ["$$name", null]}, "as": "name"}}}');

-- $filter reference variable with dotted expression
SELECT * FROM bson_dollar_project('{"a": [{"first": "john", "middle": "eric"}, {"first": "robert", "middle": null}]}', '{"result": {"$filter": {"input": "$a", "cond": {"$ne": ["$$this.middle", null]}}} }');
SELECT * FROM bson_dollar_project('{"a": [{"first": "john", "middle": "eric"}, {"first": "robert", "middle": null}]}', '{"result": {"$filter": {"input": "$a", "cond": {"$ne": ["$$person.middle", null]}, "as": "person"}}}');

-- $filter with limit
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": 1}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberLong": "1"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDecimal": "5.00000"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDouble": "1.0"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": null}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": "$undefinedPath"}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": 5}} }');
SELECT * FROM bson_dollar_project('{"a": ["robert", "john", null, "david", null, null, "eric"]}', '{"result": {"$filter": {"input": "$a", "cond": {"$ne": ["$$this", null]}, "limit": 4}} }');

-- $filter with nested filter and clashing variable names
SELECT * FROM bson_dollar_project('{"a": [[-100, 10, 20, 30, 40, 8000]]}', '{"result": {"$filter": {"input": "$a", "limit": 2, "cond": {"$filter": {"input": "$$this", "limit": 3, "cond": {"$gt": [{"$add": [0, "$$this"]}, 3]}}}}}}');
SELECT * FROM bson_dollar_project('{"a": [[-100, 10, 20, 30, 40, 8000]]}', '{"result": {"$filter": {"input": "$a", "limit": 2, "cond": {"$filter": {"input": "$$this", "limit": 3, "cond": {"$gt": [{"$add": [0, "$$number"]}, 3]}, "as": "number"}}}}}');
SELECT * FROM bson_dollar_project('{"a": [[-100, 10, 20, 30, 40, 8000]]}', '{"result": {"$filter": {"input": "$a", "limit": 2, "as": "number", "cond": {"$filter": {"input": "$$number", "limit": 3, "cond": {"$gt": [{"$add": [0, "$$number"]}, 3]}, "as": "number"}}}}}');

-- $filter with $and/$or shouldn't fail when if condition is met before error in nested expression
SELECT * FROM bson_dollar_project('{"a": [-1,-2,-3,-4]}', '{"result": {"$filter": {"input": "$a", "cond": {"$or": [{"$lt": ["$$this", 0]}, {"$ln": "$$this"}]}}}}');
SELECT * FROM bson_dollar_project('{"a": [-1,-2,-3,-4]}', '{"result": {"$filter": {"input": "$a", "cond": {"$and": [{"$gt": ["$$this", 0]}, {"$ln": "$$this"}]}}}}');

-- $filter, arg must be an object
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": {"$filter": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": {"$filter": true}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": {"$filter": []}}');

-- $filter, object should have cond and input, as/limit are optional, any other path is invalid
SELECT * FROM bson_dollar_project('{"a": [-1,-2,-3,-4]}', '{"result": {"$filter": {"input": "$a"}}}');
SELECT * FROM bson_dollar_project('{"a": [-1,-2,-3,-4]}', '{"result": {"$filter": {"cond": true}}}');
SELECT * FROM bson_dollar_project('{"a": [-1,-2,-3,-4]}', '{"result": {"$filter": {"input": "$a", "cond": true, "sort": false}}}');

-- $filter, input should be an array
SELECT * FROM bson_dollar_project('{"a": { }}', '{"result": {"$filter": {"input": "$a", "cond": true}}}');
SELECT * FROM bson_dollar_project('{"a": { }}', '{"result": {"$filter": {"input": "a", "cond": true}}}');
SELECT * FROM bson_dollar_project('{"a": { }}', '{"result": {"$filter": {"input": true, "cond": true}}}');

-- $filter, as variable name should be valid
-- invalid
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "_element"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "1element"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "Element"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "el@ement"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "el-ement"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": ""}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": null}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": true}}}');
-- valid
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "element1"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "el_ement1"}}}');
SELECT * FROM bson_dollar_project('{"a": []}', '{"result": {"$filter": {"input": "$a", "cond": true, "as": "eLEMEnt"}}}');

-- $filter, limit must be representable as a 32bit integer and > 0
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDouble": "1.1"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDecimal": "1.1"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberLong": "2147483648"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDouble": "2147483648.0"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDouble": "0.0"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": {"$numberDouble": "-1.0"}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2]}', '{"result": {"$filter": {"input": "$a", "cond": true, "limit": -100}} }');

-- $filter, undefined variable
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "as":"element", "cond": {"$gt": ["$$this", 3]}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "cond": {"$gt": ["$$element", 3]}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "cond": {"$gt": ["$$this", "$$filterValue"]}}} }');
SELECT * FROM bson_dollar_project('{"a": [1, 2, 3, 4, 5, 6]}', '{"result": {"$filter": {"input": "$a", "cond": {"$gt": ["$$this", "$$filterValue.name.last"]}}} }');


-- $firstN, operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":1} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":11} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":100} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [], "n":100} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":3.000000} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":{"$numberDecimal": "2.000000"}} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n": {"$numberDecimal": "9223372036854775807"} } }}');

-- $firstN, operator with docs in db 
select documentdb_api.insert_one('db','dollarfirstN','{"a":[1,2,3] , "_id":1}');
select documentdb_api.insert_one('db','dollarfirstN','{"a":[42,67] , "_id":2}');
select documentdb_api.insert_one('db','dollarfirstN','{"a":[] , "_id":3}');
select documentdb_api.insert_one('db','dollarfirstN','{"a":[1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"] , "_id":4}');
select bson_dollar_project(document, '{"result": { "$firstN": {"input": "$a", "n":3} }}') from documentdb_api.collection('db','dollarfirstN');
-- $firstN , operator with docs in db and invalid path
select documentdb_api.insert_one('db','dollarFirstN','{"b":[] , "_id":5}');
select bson_dollar_project(document, '{"result": { "$firstN": {"input": "$a", "n":3} }}') from documentdb_api.collection('db','dollarFirstN');
select bson_dollar_project(document, '{"result": { "$firstN": {"input": "$a", "n":"abc"} }}') from documentdb_api.collection('db','dollarFirstN');

--$firstN, operator invalid input
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": true, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": 5, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": "abcvde", "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": 4.56, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": true }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": "ayush_test_name" }}');


--$firstN, operator invalid n
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":"3"} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":0} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":-1} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n":3.124} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n": {"$numberDecimal": "9223372036854775808"} } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4], "n": {"$numberDecimal": "0"} } }}');

-- $lastN, operator with docs in db 
select documentdb_api.insert_one('db','dollarLastN','{"a":[1,2,3] , "_id":1}');
select documentdb_api.insert_one('db','dollarLastN','{"a":[42,67] , "_id":2}');
select documentdb_api.insert_one('db','dollarLastN','{"a":[] , "_id":3}');
select documentdb_api.insert_one('db','dollarLastN','{"a":[1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"] , "_id":4}');
select bson_dollar_project(document, '{"result": { "$lastN": {"input": "$a", "n":3} }}') from documentdb_api.collection('db','dollarLastN');
-- $lastN , operator with docs in db and invalid path
select documentdb_api.insert_one('db','dollarLastN','{"b":[] , "_id":5}');
select bson_dollar_project(document, '{"result": { "$lastN": {"input": "$a", "n":3} }}') from documentdb_api.collection('db','dollarLastN');
select bson_dollar_project(document, '{"result": { "$lastN": {"input": "$a", "n":"abc"} }}') from documentdb_api.collection('db','dollarLastN');


--$firstN, operator missing or extra args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"input": [1,2,3,4]} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"n": 5} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$firstN": {"n": 5, "input": [1,2,3], "abc":1} }}');


-- $lastN, operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":1} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":11} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,45,12,null,"asdhkas",12890.8912,{"$numberDecimal": "223.234823904823904823041212233"},{"$date": { "$numberLong" : "0" }},"\ud0008"], "n":100} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [], "n":100} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":3.000000} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":{"$numberDecimal": "2.000000"}} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n": {"$numberDecimal": "9223372036854775807"} } }}');

--$lastN, operator invalid input
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": true, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": 5, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": "abcvde", "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": 4.56, "n":3} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": true }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": "ayush_test_name" }}');


--$lastN, operator invalid n
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":"3"} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":0} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":-1} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n":3.124} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n": {"$numberDecimal": "9223372036854775808"} } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4], "n": {"$numberDecimal": "0"} } }}');

--$lastN, operator missing or extra args
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"input": [1,2,3,4]} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"n": 5} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$lastN": {"n": 5, "input": [1,2,3], "abc":1} }}');


-- $range, simple condition asc
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,10] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,10,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,100,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [{"$numberLong": "50"},100,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [50,{"$numberLong": "150"},{"$numberDecimal":"20.0000"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [50,{"$numberDecimal": "150"},{"$numberDouble":"20.00000000"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [50,{"$numberDecimal": "50"},{"$numberDouble":"20.00000000"}] } }');

-- $range, simple condition desc
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [10,-10,-1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [10,1,-3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [100,1,-3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [{"$numberLong": "100"},-50,-3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [150,{"$numberLong": "50"},{"$numberDecimal":"-20.0000"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [150,{"$numberDecimal": "50"},{"$numberDouble":"-20.00000000"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [150,{"$numberDecimal": "150"},{"$numberDouble":"-20.00000000"}] } }');

-- $range, asc series but skip val negative
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,10,-1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,10,{"$numberDecimal":"-1.0000"}] } }');


-- $range, desc series but skip val positive
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [10,1,1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [10,1,{"$numberDecimal":"1.0000"}] } }');


-- $range, expects only ranges in int_32 hence, if numbers overflow during range we should stop
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,2147483647,1073741824] } }');

-- $range, testing with values from document
SELECT * FROM bson_dollar_project('{"city": "San Jose", "distance": 42}', '{"result": {"$range": ["$distance",200,40] } }');
SELECT * FROM bson_dollar_project('{"city": "San Jose", "distance": 88}', '{"result": {"$range": ["$distance",200,40] } }');
SELECT * FROM bson_dollar_project('{"city": "San Jose", "distance": 288}', '{"result": {"$range": ["$distance",200,40] } }');
SELECT * FROM bson_dollar_project('{"city": "San Jose", "distance": 288}', '{"result": {"$range": [10,"$distance",5] } }');

-- $range, negative conditions starting number
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [2147483648,214748364121,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [{"$numberDecimal":"0.35"},400,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": ["abvcf",400,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [true,400,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [null,400,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [null,400,40,400] } }');

-- $range, negative conditions ending number
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [2147648,214748364121,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [-10, {"$numberDecimal":"0.35"},4] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [400,"abvcf",400] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [5,true,40] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,null] } }');

-- $range, negative conditions step number
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [21,21471,2147483648] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [-10,15, {"$numberDecimal":"0.35"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,400,"abvcf"] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [5,40,true] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,5,null] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [1,5,0] } }');

-- $range, tests large numbers
SELECT * FROM bson_dollar_project('{}', '{"result": {"$size":{"$range": [200,20000] }} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range": [200,20000] },0]} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range": [200,20000] },19799]} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$size":{"$range": [50000,65535960,100000] }} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range": [50000,65535960,100000] },0]} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range": [50000,65535960,100000] },654]} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$size":{"$range":  [2000000,1073741924,1000000] }} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range":  [2000000,1073741924,1000000] },0]} }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$arrayElemAt":[{"$range":  [2000000,1073741924,1000000] },1071]} }');

-- $range, tests memory limit exceed
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [200,65535960] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [50000,65535960] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [0,1073741924] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$range": [0,6505360] } }');

-- $reverseArray, simple case
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1,2,3,4,5,6] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1,2,3,4,5,6,{"$numberLong":"1"}, {"$numberDecimal":"12389.2134234"}] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1,2,3,4, {"$add":[1,2]}] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1,2,3,4, {"$add":[1,2]}, "$field"] } } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$literal": [1,2,3,4, {"$add":[1,2]}, "$field", [1.45,9,9]] } } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": "$nums" } }');
SELECT * FROM bson_dollar_project('{"key":[{"a":[8,9,10]},{"a":[3,2,1,-1]},{"a":[4,5,6]}]}', '{"result": {"$reverseArray": "$key.a" } }');

-- $reverseArray, operator cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$range":[1,4]} } }');


-- $reverseArray, null cases
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": "$noField" } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": null } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$reverseArray": {"$undefined":true} } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": {"$literal":null} } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": {"$literal":{"$undefined":true}} } }');

-- $reverseArray, negative cases
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": [1,2,345,5] } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": true } }');
SELECT * FROM bson_dollar_project('{"nums": [1,2,3,4, {"$add":[1,2]}, "$field", [145,9,9]]}', '{"result": {"$reverseArray": 1 } }');

-- $indexOfArray, simple cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5], 1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[], 1,1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [null, 1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5], 2] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5], 4] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5], 5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50, 0, 111] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50, 111, 111] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50, 2, 5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50, 2, 7] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50, 7, 7] } }');

-- $indexOfArray, document based array
SELECT * FROM bson_dollar_project('{"a": [1,2,345,5]}', '{"result": {"$indexOfArray": ["$a", 1] } }');
SELECT * FROM bson_dollar_project('{"a": [[1,2,345,5]]}', '{"result": {"$indexOfArray": ["$a", [1]] } }');
SELECT * FROM bson_dollar_project('{"a": [[1,2,345,5]]}', '{"result": {"$indexOfArray": ["$a", [1,2]] } }');

-- $indexOfArray, negative cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [true, 50.0, 1,50] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 11, 5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, -1, 5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1, -5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1.1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1,5.11] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1,5.11] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1,5,4] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, "1",5] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1,"50"] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,2,345,5,100,90,11,50,234], 50.0, 1,90000000000000] } }');

-- $indexOfArray, search for elements of different type
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,{"a":1}], "2"] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,{"a":1}], true] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,{"a":1}], {"a":1}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,{"$numberLong":"44374"}], {"$numberLong":"44374"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,{"$numberDecimal":"44374.124"}], {"$numberDecimal":"44374.124"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true,null], null] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true, {"$date": { "$numberLong" : "0" }}, null], {"$date": { "$numberLong" : "0" }}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1,"2",true, "This is ❤️", null], "This is ❤️"] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 44374, "2", true, {"$numberLong":"44374"}], {"$numberLong":"44374"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 44374, "2", true, {"$numberLong":"44374"}], {"$numberInt":"44374"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", true, {"$numberLong":"44374"}], {"$numberDecimal":"44374"}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", [true, {"$numberLong":"44374"}]], [true,{"$numberDecimal":"44374"}]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", [true, {"$numberLong":"44374"}, 1]], [1,true,{"$numberDecimal":"44374"}]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", {"a":{"b":24, "c":1,"d":2}}], {"a":{"b":24, "d":2, "c":1 }}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", {"a":{"b":24, "c":1,"d":2, "e": null}}], {"a":{"b":24,"c":1, "d":2, "e":null }}] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$indexOfArray": [[1, 4374, "2", {"a":{"b":24, "c":1,"d":2, "e": null}}], {"a":{"b":24,"c":1, "d":2, "e": {"$undefined":true} }}] } }');


-- $max, single input cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": null } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {"$undefined":true} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {"a":1} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": 5 } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {"$numberDouble":"5.454"} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {"$numberDecimal":"5.454"} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": {"$date": { "$numberLong" : "0" }} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [-2147483648, 2147483647, -9223372036854775808, 9223372036854775807, 0, 100, -100] } }');

-- $max, array input cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [1,2,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [[1,2,3]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [1,2,3,5,3,2,1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [100,2,3,5,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [100,2,33, [1,2,3]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [100,200.0] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}] } }');

-- $max, null cases within array
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [null, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [{}, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [{"$undefined": true}, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [null, NaN, 1, 2] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max": [{}, NaN, 1, 2] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$max":  [1,{"$undefined":true},8,3,4,[1,2,3]] } }');

-- $max, input document cases
SELECT * FROM bson_dollar_project('{"a": 5}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": 130.5}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": NaN}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [1,2,3,5,3,2,1]}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [1,2,3,5,3,2,1]}', '{"result": {"$max": ["$a", 223324342] } }');
SELECT * FROM bson_dollar_project('{"a": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}]}', '{"result": {"$max": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}]}', '{"result": {"$max": [1,2,45, "$a"] } }');

-- $min, single input cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": null } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {"$undefined":true} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {"a":1} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": 5 } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {"$numberDouble":"5.454"} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {"$numberDecimal":"5.454"} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": {"$date": { "$numberLong" : "0" }} } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [-2147483648, 2147483647, -9223372036854775808, 9223372036854775807, 0, 100, -100] } }');

-- $min, array input cases
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [1,2,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [[1,2,3]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [1,2,3,5,3,2,1] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [100,2,3,5,3] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [100,2,33, [1,2,3]] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [100,200.0] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}] } }');

-- $min, null cases within array
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [null, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [{}, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [{"$undefined": true}, NaN] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [null, NaN, 1, 2] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min": [{}, NaN, 1, 2] } }');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$min":  [1,{"$undefined":true},8,3,4,[1,2,3]] } }');

-- $min, input document cases
SELECT * FROM bson_dollar_project('{"a": 5}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": 130.5}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": NaN}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [1,2,3,5,3,2,1]}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [1,2,3,5,3,2,1]}', '{"result": {"$min": ["$a", 223324342] } }');
SELECT * FROM bson_dollar_project('{"a": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}]}', '{"result": {"$min": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [100,2,33, [1,2,3], {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "5.12124545487878787"},{}, NaN , {"$regex": "/ab/cdsd/abc", "$options" : ""}]}', '{"result": {"$min": [1,2,45, "$a"] } }');

-- $sum , single input
SELECT * FROM bson_dollar_project('{"a": 5}', '{"result": {"$sum": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": "foo" }', '{"result": {"$sum": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": null }', '{"result": {"$sum": "$a" } }');
SELECT * FROM bson_dollar_project('{"b": null }', '{"result": {"$sum": "$a" } }');

-- $sum , array input
SELECT * FROM bson_dollar_project('{"a": [ 7, 1, 2 ] }', '{"result": {"$sum": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [ "foo", 1, 2 ] }', '{"result": {"$sum": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [ 1, null, 2 ] }', '{"result": {"$sum": "$a" } }');

-- $sum , array expression
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": 3 }', '{"result": {"$sum": [ "$a", "$b", "$c" ] } }');
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": 3 }', '{"result": {"$sum": [ "$a", { "$subtract": [ "$c", "$b" ] } ] } }');

-- $avg , single input
SELECT * FROM bson_dollar_project('{"a": 5}', '{"result": {"$avg": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": "foo" }', '{"result": {"$avg": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": null }', '{"result": {"$avg": "$a" } }');
SELECT * FROM bson_dollar_project('{"b": null }', '{"result": {"$avg": "$a" } }');

-- $avg , array input
SELECT * FROM bson_dollar_project('{"a": [ 7, 1, 2 ] }', '{"result": {"$avg": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [ "foo", 1, 2 ] }', '{"result": {"$avg": "$a" } }');
SELECT * FROM bson_dollar_project('{"a": [ 1, null, 2 ] }', '{"result": {"$avg": "$a" } }');

-- $avg , array expression
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": 3 }', '{"result": {"$avg": [ "$a", "$b", "$c" ] } }');
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": 3 }', '{"result": {"$avg": [ "$a", { "$subtract": [ "$c", "$b" ] } ] } }');

-- $map
select *from bson_dollar_project('{"a": [1, 2, 3]}', '{"result": {"$map": {"input": "$a", "as": "x", "in": { "$add": ["$$x", 1] } } } }');
select *from bson_dollar_project('{"str": ["a", "b", "c"]}', '{"result": {"$map": {"input": "$str", "as": "y", "in": { "$concat": ["$$y", "ddd"] } } } }');
select *from bson_dollar_project('{"bools": [true, true, false, true] }', '{"allTrue": { "$map": { "input": "$bools", "as": "x", "in": { "$and": ["$$x", false] }   } } }');
select *from bson_dollar_project('{ "Precinto": ["Hello", "", "big", "World", "!"] }', '{"Precinto": { "$map": { "input": "$Precinto", "as": "x", "in": { "$concat": [{ "$trim": { "input" : "$$x" }}, {"$cond": {"if": { "$eq": [ "$$x", "" ] }, "then": "*", "else": "#" } }, { "$trim": { "input" : "$$x" } } ] } } } }');
select *from bson_dollar_project('{"a": [[1, 2], [2, 3], [3, 4]] }', '{"uniqueSet": {"$map": {"input": "$a","as": "x","in": { "$setUnion": ["$$x", [1,2]] }}}}');
select *from bson_dollar_project('{"outerArray": [{"innerArray": [1, 2, 3]},{"innerArray": [4, 5, 6]}]}', '{"result": {"$map": {"input": "$outerArray","as": "outerElement","in": {"$map": {"input": "$$outerElement.innerArray","as": "innerElement","in": { "$multiply": ["$$innerElement", 2] }}}}}}');

-- $map in $addfield stage
SELECT documentdb_api.insert_one('db','map_in_addfield','{"_id":"1", "a": [{ "name": "Alice Smith", "country": "USA" },{ "name": "Bob Johnson", "country": "Canada" }]}', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "map_in_addfield", "pipeline": [ { "$addFields": {"newField": { "$map": { "input": "$a", "as": "author", "in": {"name_country": { "$concat": ["$$author.name", " from ", "$$author.country"] }} } } } } ], "cursor": {} }');

-- $map, nested case
select *from bson_dollar_project('{"items":[{"name":"a","quantity":10,"sizes":[{"name":"ab", "quantity":2}, {"name":"ac","quantity":3}]},{"name":"k","quantity":3,"sizes":[{"name":"kb", "quantity":3}, {"name":"kc", "quantity":5}]}]}', '{"items":{"$map":{"input":"$items","as":"item","in":{"name":"$$item.name","quantity":"$$item.quantity","sizes":{"$map":{"input":"$$item.sizes","as":"size","in":{"name":"$$size.name","quantity":{"$multiply":["$$size.quantity",2]}}}}}}}}');

-- $map, negative cases
select *from bson_dollar_project('{"a": 1}', '{"result": {"$map": {"input": "$a", "as": "x", "in": { "$add": ["$$x", 1] } } } }');
select *from bson_dollar_project('{"a": [1, 2, 3]}', '{"result": {"$map": {"input": "$a" }} }');

-- $reduce
select *from bson_dollar_project('{"a": ["a", "b", "c"]}', '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$value", "$$this"] } } } }');
select *from bson_dollar_project('{"a": [ 1, 2, 3, 4 ]}', '{"result": { "$reduce": { "input": "$a", "initialValue": { "sum": 5, "product": 2 }, "in": { "sum": { "$add" : ["$$value.sum", "$$this"] }, "product": { "$multiply": [ "$$value.product", "$$this" ] }  } } } }');
select *from bson_dollar_project('{"a": [ [ 3, 4 ], [ 5, 6 ] ] }', '{"result": { "$reduce": { "input": "$a", "initialValue": [ 1, 2 ], "in": { "$concatArrays" : ["$$value", "$$this"] } } } }');
select *from bson_dollar_project('{"matrix": [[1, 2], [3, 4], [5, 6]] }', '{"sumOfMatrix": { "$reduce": { "input": "$matrix", "initialValue": 0, "in": { "$add" : ["$$value", {"$reduce": {"input": "$$this","initialValue": 0, "in": { "$add": ["$$value", "$$this"] }}}] } } } }');
select *from bson_dollar_project('{ "Precinto": ["Hello", "", "big", "World", "!"] }', '{"Precinto": { "$reduce": { "input": "$Precinto", "initialValue": "", "in": { "$concat": [{ "$trim": { "input" : "$$value" }}, {"$cond": {"if": { "$eq": [ "$$value", "" ] }, "then": "*", "else": "#" } }, { "$trim": { "input" : "$$this" } } ] } } } }');
select *from bson_dollar_project('{"a": [2, 3, 5, 8, 13, 21,7,1]}','{"result": {"$reduce": { "input": "$a", "initialValue": 0, "in": { "$cond": [{ "$gt": ["$$this", 5] }, { "$add": ["$$value", 1] }, "$$value"] } } } }');
select *from bson_dollar_project('{"bools": [true, true, false, true] }', '{"allTrue": { "$reduce": { "input": "$bools", "initialValue": true, "in": { "$and": ["$$value", "$$this"] }   } } }');
select *from bson_dollar_project('{"a": [1, null, 3, null, 5] }', '{"total": { "$reduce": { "input": "$a", "initialValue": 0, "in": { "$add": ["$$value", { "$ifNull": ["$$this", 0] }] }  } } }');
select *from bson_dollar_project('{"a": [[1, 2], [2, 3], [3, 4]] }', '{"uniqueSet": {"$reduce": {"input": "$a","initialValue": [],"in": { "$setUnion": ["$$value", "$$this"] }}}}');
select *from bson_dollar_project('{"a": [{ "a": 1 }, { "b": 2 }, { "a": 2 }, { "b": 3 },{ "c": 3 }, { "c": 4 }] }','{"uniqueSet":{"$reduce": {"input": "$a","initialValue": {},"in":{ "$mergeObjects": ["$$value", "$$this"] }}}}');
select *from bson_dollar_project('{"a": [[1, 2], [3, 4], [5, 6]] }','{"uniqueSet":{"$reduce": {"input": "$a","initialValue": 0,"in":{ "$sum": ["$$value",{ "$avg": "$$this" }] }}}}');

-- $reduce in $addfield stage
SELECT documentdb_api.insert_one('db','reduce_in_addfield','{"_id":"1", "a": ["a", "b", "c"]}', NULL);
SELECT document FROM bson_aggregation_pipeline('db', '{ "aggregate": "reduce_in_addfield", "pipeline": [ { "$addFields": {"newField": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$value", "$$this"] } } } } } ], "cursor": {} }');

-- $reduce, negative cases
select *from bson_dollar_project('{"a": 1}', '{"result": { "$reduce": { "input": "$a", "initialValue": "", "in": { "$concat": ["$$value", "$$this"] } } } }');
select *from bson_dollar_project('{"a": [1, 2, 3]}', '{"result": {"$reduce": {"input": "$a", "initialValue": 0 }} }');
select *from bson_dollar_project('{"a": [1, 2, 3]}', '{"result": {"$reduce": {"input": "$a", "in": { "$concat": ["$$value", "$$this"] } }} }');

-- $maxN/minN, n too large
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": {"input": [1.1,2.2,"3"], "n": 23 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": {"input": [1.1,2.2,"3"], "n": 23 }   }}');

SELECT * FROM bson_dollar_project('{}', '{"result": { "$maxN": {"input":   [1, 2, 3, 5, 7, 9], "n": {"$numberDecimal": "12345678901234567890" } }   }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$minN": {"input":   [1, 2, 3, 5, 7, 9], "n": {"$numberDecimal": "12345678901234567890" } }   }}');

-- $maxN/minN, n short
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": {"input": [1.1,2.2,"3"], "n": 2 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": {"input": [1.1,2.2,"3"], "n": 2 }   }}');

-- $maxN/minN, n == 1
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": {"input": [1.1,2.2,"3"], "n": 1 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": {"input": [1.1,2.2,"3"], "n": 1 }   }}');

-- $maxN/minN, n missing
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": {"input": [1.1,2.2,"3"] }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": {"input": [1.1,2.2,"3"] }   }}');

-- $maxN/minN, input missing
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": { "n": 1 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": { "n": 1 }   }}');

-- $maxN/minN, n and input missing
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": { "x": 1 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$minN": { "x": 1 }   }}');

-- $maxN/minN, use input from $d1
SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1]}', '{"result": { "$maxN": {"input": "$d1", "n": 23 }   }}');

SELECT * FROM bson_dollar_project('{"d1": [1.1,2.1,3.1,4,5,6,7,8, 99,99,99,99,99]}', '{"result": { "$maxN": {"input": "$d1", "n": 2 }   }}');

-- $maxN/minN, tests from official jstests
SELECT * FROM bson_dollar_project('{}', '{"result": { "$maxN": {"input":  [null, 2, null, 1], "n": 3 }   }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$minN": {"input":  [null, 2, null, 1], "n": 3 }   }}');

SELECT * FROM bson_dollar_project('{}', '{"result": { "$maxN": {"input":   [1, 2, 3, 5, 7, 9], "n": 3 }   }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$minN": {"input":   [1, 2, 3, 5, 7, 9], "n": 3 }   }}');

--$maxN/minN, nested array
SELECT * FROM bson_dollar_project('{"d1": [[1,2,3],[4,5,6],[7,8,9]]}', '{"result": { "$maxN": {"input": "$d1", "n": 3 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [[1,2,3],[4,5,6],[7,8,9]]}', '{"result": { "$minN": {"input": "$d1", "n": 3 }   }}');

SELECT * FROM bson_dollar_project('{"d1": [[4,5,6],[7,8,9],1,2,3]}', '{"result": { "$maxN": {"input": "$d1", "n": 5 }   }}');
SELECT * FROM bson_dollar_project('{"d1": [[7,8,9],[4,5,6],1,2,3]}', '{"result": { "$minN": {"input": "$d1", "n": 5 }   }}');

--$maxN/minN, numberDecimal type array
SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "2.23456789" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$maxN": {"input": "$d1", "n": 4 } } }');
SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "2.23456789" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$minN": {"input": "$d1", "n": 4 } } }');

SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "2.23456789" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "-1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$maxN": {"input": "$d1", "n": 4 } } }');
SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "2.23456789" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "-1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$minN": {"input": "$d1", "n": 4 } } }');

SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "NaN" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$maxN": {"input": "$d1", "n": 4 } } }');
SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "NaN" },{"$numberDecimal": "1.23456789" }, { "$numberDecimal": "1.32456789" }, { "$numberDecimal": "1.34256789" }]}', '{"result": { "$minN": {"input": "$d1", "n": 4 } } }');

SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "NaN" }, 1, 3, -2, null, [3, {"numberDouble": "NaN"}], {"$numberDecimal": "Infinity" }]}', '{"result": { "$maxN": {"input": "$d1", "n": 7 } } }');
SELECT * FROM bson_dollar_project('{"d1": [{ "$numberDecimal": "NaN" }, 1, 3, -2, null, [3, {"numberDouble": "NaN"}], {"$numberDecimal": "Infinity" }]}', '{"result": { "$minN": {"input": "$d1", "n": 7 } } }');

-- $sortArray
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortBy": -1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": ["apple", "banana", "orange", "pear"], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": ["apple", "banana", "orange", "pear"], "sortBy": -1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [42], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [20, 4, { "a": "Free" }, 6, 21, 5, "Gratis", { "a": null }, { "a": { "sale": true, "price": 19 } }, {"$numberDecimal": "10.23"}, { "a": "On sale" }], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [[6,2,3],[4,8,6], 4, 1, 6, 12, 5], "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [{"$numberDecimal": "NaN"}, 4, 1, 6, 12, 5, null, " "], "sortBy": 1 } } }');
select *from bson_dollar_project('{"team": [{ "name":"pat","age":30,"address":{"street":"12 Baker St","city":"London"}}, {"name":"dallas","age":36,"address":{"street":"12 Cowper St","city":"Palo Alto"}}, {"name":"charlie","age":42,"address":{"street":"12 French St","city":"New Brunswick" }}]}', '{"result": {"$sortArray": {"input": "$team", "sortBy": { "name": 1 } } } }');
select *from bson_dollar_project('{"team": [{ "name":"pat","age":30,"address":{"street":"12 Baker St","city":"London"}}, {"name":"dallas","age":36,"address":{"street":"12 Cowper St","city":"Palo Alto"}}, {"name":"charlie","age":42,"address":{"street":"12 French St","city":"New Brunswick" }}]}', '{"result": {"$sortArray": {"input": "$team", "sortBy": { "name": -1 } } } }');
select *from bson_dollar_project('{"team": [{ "name":"pat","age":30,"address":{"street":"12 Baker St","city":"London"}}, {"name":"dallas","age":36,"address":{"street":"12 Cowper St","city":"Palo Alto"}}, {"name":"charlie","age":42,"address":{"street":"12 French St","city":"New Brunswick" }}]}', '{"result": {"$sortArray": {"input": "$team", "sortBy": { "address.city": 1 } } } }');
select *from bson_dollar_project('{"team": [{ "name":"pat","age":30,"address":{"street":"12 Baker St","city":"London"}}, {"name":"dallas","age":36,"address":{"street":"12 Cowper St","city":"Palo Alto"}}, {"name":"charlie","age":42,"address":{"street":"12 French St","city":"New Brunswick" }}]}', '{"result": {"$sortArray": {"input": "$team", "sortBy": { "address.city": -1 } } } }');
select *from bson_dollar_project('{"team": [{ "name":"pat","age":30,"address":{"street":"12 Baker St","city":"London"}}, {"name":"dallas","age":36,"address":{"street":"12 Cowper St","city":"Palo Alto"}}, {"name":"charlie","age":42,"address":{"street":"12 French St","city":"New Brunswick" }}]}', '{"result": {"$sortArray": {"input": "$team", "sortBy": { "age": -1, "name": 1} } } }');

-- $sortArray, negative cases
select *from bson_dollar_project('{}', '{"result": {"$sortArray": null } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": " " } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": "", "sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"sortBy": 1 } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4] } } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": "", "sortBy": 1} } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": {}, "sortBy": 1} } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortByy": 1} } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortBy": 2} } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortBy": ""} } }');
select *from bson_dollar_project('{}', '{"result": {"$sortArray": {"input": [1, 5, 3, 2, 4], "sortBy": {}} } }');

-- $zip
select *from bson_dollar_project('{"a": [[1, 2, 3], [4, 5, 6]]}', '{"result": {"$zip": {"inputs": "$a"} } }');
select *from bson_dollar_project('{"multitypes": [[1, true, {"value": 2}], ["a", [3, 2], "Great"]]}', '{"result": {"$zip": {"inputs": "$multitypes"} } }');
select *from bson_dollar_project('{"diffLen": [[ "a", "b" ], [ "b", "c", "d" ]]}', '{"result": {"$zip": {"inputs": "$diffLen"} } }');
select *from bson_dollar_project('{"useLongest": [[ 0, 1 ], [ 2, 3, 4, 5 ]]}', '{"result": {"$zip": {"inputs": "$useLongest", "useLongestLength": true} } }');
select *from bson_dollar_project('{"falseUseLongest": [[ 0 ], [ 2, 3 ]]}', '{"result": {"$zip": {"inputs": "$falseUseLongest", "useLongestLength": false} } }');
select *from bson_dollar_project('{"withDefaults": [[ 0 ], [ 1, 2 ], [ 3 ]]}', '{"result": {"$zip": {"inputs": "$withDefaults", "useLongestLength": true, "defaults": [ "a", "b", "c" ]} } }');
select *from bson_dollar_project('{"emptyArray": [[], [4, 5, 6]]}', '{"result": {"$zip": {"inputs": "$emptyArray"} } }');
select *from bson_dollar_project('{"emptyArrayUseLongest": [[], [4, 5, 6]]}', '{"result": {"$zip": {"inputs": "$emptyArrayUseLongest", "useLongestLength": true} } }');
select *from bson_dollar_project('{"nullELemArray": [[null, null], [4, 5, 6]]}', '{"result": {"$zip": {"inputs": "$nullELemArray"} } }');
select *from bson_dollar_project('{"withDefaults": [[ 0 ], [ 1, 2 ], [ 3 ]]}', '{"result": {"$zip": {"inputs": "$withDefaults", "useLongestLength": true, "defaults": "$withDefaults"} } }');
select *from bson_dollar_project('{"array1": [1, 2, 3], "array2": ["a", "b", "c"], "array3": ["x", "y", "z"] }', '{"result": {"$zip": {"inputs": [{ "$zip": { "inputs": ["$array1", "$array2"]}}, "$array3" ]} } }');

-- $zip, negative cases
select *from bson_dollar_project('{"a": [[ 0 ], [ 1, 2 ], [ 3 ]]}', '{"result": {"$zip": {"inputs": "$a", "defaults": [ "a", "b", "c" ]} } }');
select *from bson_dollar_project('{"a": [[ 0 ], [ 1, 2 ], [ 3 ]]}', '{"result": {"$zip": {"inputs": "$a", "useLongestLength": true, "defaults": [ "a", "b" ]} } }');
select *from bson_dollar_project('{"a": []}', '{"result": {"$zip": {"inputs": "$a"} } }');
select *from bson_dollar_project('{"a": null}', '{"result": {"$zip": {"inputs": "$a"} } }');
select *from bson_dollar_project('{"a": [1]}', '{"result": {"$zip": {"inputs": "$a"} } }');
select *from bson_dollar_project('{"a": [[1,2,3], "anString"]}', '{"result": {"$zip": {"inputs": "$a"} } }');
select *from bson_dollar_project('{"a": [[0], [1, 2]]}', '{"result": {"$zip": {"inputs": "$a", "useLongestLength": 0} } }');
select *from bson_dollar_project('{"a": [[0], [1, 2]]}', '{"result": {"$zip": {"inputs": "$a", "useLongestLength": true, "defaults": 0} } }');
