SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 6800000;
SET documentdb.next_collection_id TO 6800;
SET documentdb.next_collection_index_id TO 6800;


-- $add operator
-- Returns expected result
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$add": [ "$a", "$b", 0 ]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": 10, "c": 10}}', '{"result": { "$add": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$add": [ "$a", 1]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$add": [ "$a", 1]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", 86400000]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", 100]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", {"$numberDouble": "43200000.56"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", {"$numberDecimal": "1e10"}]}}');

-- A single expression is also valid
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$add": "$a"}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$add": "$b"}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$add": NaN}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$add": null}}');

-- Int32 overflow to -> Int64
SELECT * FROM bson_dollar_project('{"a":2147483646}', '{"result": { "$add": [ "$a", 2]}}');
SELECT * FROM bson_dollar_project('{"a":1073741823, "b": 1073741825}', '{"result": { "$add": [ "$a", "$b"]}}');

-- Int64 overflow coerce to double
SELECT * FROM bson_dollar_project('{"a":9223372036854775807}', '{"result": { "$add": [ "$a", 2]}}');

-- Any null should project to null (even if it is a date overflow), non-existent paths return null if there is no date overflow
SELECT * FROM bson_dollar_project('{"a":null, "b": 2}', '{"result": { "$add": [ "$a", "$b", 0 ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$add": [ "$a", "$b", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": null}', '{"result": { "$add": [ "$a", "$b", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$add": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$add": [ "$a", "$a.b" ]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "9223372036854775807" }} }', '{"result": { "$add": [ "$a", 100, 100, null ]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "9223372036854775807" }}, "b": null }', '{"result": { "$add": [ "$a", 100, 100, "$b" ]}}');

-- Should return invalid date (INT64_MAX) if there is a decimal128 overflow
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "1e5000"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": { "$numberLong" : "0" }}, {"$numberDecimal": "Infinity"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": { "$numberLong" : "0" }}, {"$numberDouble": "NaN"}, {"$numberDecimal": "1"}]}}');

-- Should round to nearest even for date operations
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": "2019-01-30T07:30:10.136Z"}, {"$numberDouble": "819.4"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": "2019-01-30T07:30:10.136Z"}, {"$numberDouble": "819.56"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": "2019-01-30T07:30:10.136Z"}, {"$numberDecimal": "819.4899999"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$add": [ {"$date": "2019-01-30T07:30:10.136Z"}, {"$numberDecimal": "819.5359123621083"}]}}');

-- Should error with non-numeric or date expressions
SELECT * FROM bson_dollar_project('{"a":{ }, "b": null}', '{"result": { "$add": [ "$a", 922, 1 ]}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": false}', '{"result": { "$add": [ "$a", "$b", 1 ]}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": "false"}', '{"result": { "$add": [ "$a", "$b", 1 ]}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": [2, 3]}', '{"result": { "$add": [ "$a", "$b", 1 ]}}');
SELECT * FROM bson_dollar_project('{"a":[1,2]}', '{"result": { "$add": [ "$a.0", "$a.1", 0 ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$add": [ "$a", "string" ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$add": [ "$a", true ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$add": [ "$a", {} ]}}');

-- Should error if multiple dates are passed in
SELECT * FROM bson_dollar_project('{"a":{ "$date": { "$numberLong" : "0" }}, "b": {"$date": { "$numberLong" : "1000" }}}', '{"result": { "$add": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":{ "$date": { "$numberLong" : "0" }}, "b": {"$date": { "$numberLong" : "1000" }}}', '{"result": { "$add": [ 1, 2, "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a": 100, "b": {"$date": { "$numberLong" : "1000" }}}', '{"result": { "$add": [ 1, 2, "$a", "$b", {"$date": {"$numberLong": "1000"}} ]}}');

-- Should error if date overflows
SELECT * FROM bson_dollar_project('{"a": 100, "b": {"$date": { "$numberLong" : "9223372036854775807" }}}', '{"result": { "$add": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a": -100, "b": {"$date": { "$numberLong" : "-9223372036854775807" }}}', '{"result": { "$add": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a": -100, "b": {"$date": { "$numberLong" : "-9223372036854775807" }}}', '{"result": { "$add": [ "$a", "$b", "$c" ]}}');
SELECT * FROM bson_dollar_project('{"a": {"$numberDouble": "100.58"}, "b": {"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", "$b", {"$numberDouble": "9223372036854775800.22"} ]}}');
SELECT * FROM bson_dollar_project('{"a": {"$numberDouble": "100.58"}, "b": {"$date": { "$numberLong" : "0" }}}', '{"result": { "$add": [ "$a", "$b", NaN ]}}');

-- $subtract operator
-- Returns expected result
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$subtract": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": 10, "c": 10}}', '{"result": { "$subtract": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": -100, "c": -100}}', '{"result": { "$subtract": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$subtract": [ "$a", 1]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$subtract": [ "$a", {"$numberDecimal": "1232"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$subtract": [ "$a", {"$numberDecimal": "1232"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDecimal": "102123"}}', '{"result": { "$subtract": [ "$a", {"$numberDecimal": "-1232"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$subtract": [ "$a", -1000]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$subtract": [ "$a", 86400000]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$subtract": [ "$a", -86400000]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}}', '{"result": { "$subtract": [ "$a", {"$numberDouble": "43200000.56"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": { "$numberLong" : "0" }}, "b": {"$date": { "$numberLong" : "86000" }}}', '{"result": { "$subtract": [ "$a", "$b"]}}');

-- Int32 overflow to -> Int64
SELECT * FROM bson_dollar_project('{"a":2147483646}', '{"result": { "$subtract": [ "$a", -2]}}');
SELECT * FROM bson_dollar_project('{"a":-1073741825, "b": 1073741825}', '{"result": { "$subtract": [ "$a", "$b"]}}');

-- Int64 overflow coerce to double
SELECT * FROM bson_dollar_project('{"a":9223372036854775807}', '{"result": { "$subtract": [ "$a", -2]}}');
SELECT * FROM bson_dollar_project('{"a":-9223372036854775807}', '{"result": { "$subtract": [ "$a", 2]}}');

-- Null or undefined path should result to null
SELECT * FROM bson_dollar_project('{"a":null, "b": 2}', '{"result": { "$subtract": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$subtract": [ "$a", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": null}', '{"result": { "$subtract": [ "$b", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$subtract": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$subtract": [ "$a", "$a.b" ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$subtract": [ "string", null ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$subtract": [ {"$date":{"$numberLong": "0"}}, null ]}}');

-- Should error if no numbers or dates are passed in
SELECT * FROM bson_dollar_project('{"a":{ }}', '{"result": { "$subtract": [ "$a", 922 ]}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": false}', '{"result": { "$subtract": [ "$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$subtract": [ "string", 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$subtract": [ 1, "string"]}}');
SELECT * FROM bson_dollar_project('{"a":[1,2]}', '{"result": { "$subtract": [ "$a.0", 0 ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$subtract": [ "$a", true ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$subtract": [ "$a", {} ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$subtract": [ "$a", {} ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$subtract": [ "$a", {"$date": {"$numberLong": "2"}}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$date": {"$numberLong": "0"}}}', '{"result": { "$subtract": [ "$a", true]}}');

-- Should error if wrong number of args is provided
SELECT * FROM bson_dollar_project('{"a":{ }}', '{"result": { "$subtract": "$a" }}');
SELECT * FROM bson_dollar_project('{"a":{ }}', '{"result": { "$subtract": [] }}');
SELECT * FROM bson_dollar_project('{"a":{ }}', '{"result": { "$subtract": [{"$subtract": [1, 2, 3]}, 2, 4, 5] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$subtract": ["$a", 1, 3] }}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": "string"}', '{"result": { "$subtract": ["$a", "$b", "$c"] }}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": 2, "c": "string"}', '{"result": { "$subtract": ["$a", "$b", "$c", 2, 3, 3, 3, 3] }}');

-- $multiply operator
-- Returns expected result
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": 1}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$multiply": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": { "$numberDouble" : "NaN" }}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$multiply": ["$a", { "$numberDouble" : "NaN" }] }}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$multiply": [ "$a", "$b", 3 ]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": 10, "c": 10}}', '{"result": { "$multiply": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": -100, "c": -100}}', '{"result": { "$multiply": [ "$a.b", "$a.c", -1]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$multiply": [ "$a", 1]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$multiply": [ "$a", {"$numberDecimal": "1232"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$multiply": [ "$a", {"$numberDecimal": "1232"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDecimal": "102123"}}', '{"result": { "$multiply": [ "$a", {"$numberDecimal": "-1232"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$multiply": [ "$a", -1000]}}');

-- Int32 overflow to -> Int64
SELECT * FROM bson_dollar_project('{"a":1073741824}', '{"result": { "$multiply": [ "$a", 2]}}');
SELECT * FROM bson_dollar_project('{"a":-1073741824}', '{"result": { "$multiply": [ "$a", 2, 2]}}');

-- Int64 overflow coerce to double
SELECT * FROM bson_dollar_project('{"a":9223372036854775807}', '{"result": { "$multiply": [ "$a", 2]}}');
SELECT * FROM bson_dollar_project('{"a":-9223372036854775807}', '{"result": { "$multiply": [ "$a", 2]}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": null}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$multiply": "$a.b"}}');
SELECT * FROM bson_dollar_project('{"a":null, "b": 2}', '{"result": { "$multiply": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$multiply": [ "$a", "$b", 3, 100, null ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$multiply": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$multiply": [ "$a", "$a.b" ]}}');

-- Should error if no number is provided
SELECT * FROM bson_dollar_project('{"a":{ }}', '{"result": { "$multiply": [ "$a", 922 ]}}');
SELECT * FROM bson_dollar_project('{"a": 1, "b": false}', '{"result": { "$multiply": [ "$a", "$b"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": [ "string", 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$multiply": [ 1, "string"]}}');
SELECT * FROM bson_dollar_project('{"a":[1,2]}', '{"result": { "$multiply": [ "$a.0", 2 ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$multiply": [ "$a", true ]}}');
SELECT * FROM bson_dollar_project('{"a":1}', '{"result": { "$multiply": [ "$a", {} ]}}');
SELECT * FROM bson_dollar_project('{"a":{}}', '{"result": { "$multiply": [ "$a", 32 ]}}');

-- $divide operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [3, 2]}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$divide": ["$a", 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [{ "$numberDouble" : "NaN" }, 2]}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$divide": ["$a", { "$numberDouble" : "NaN" }] }}');
SELECT * FROM bson_dollar_project('{"a":{"b": 10, "c": 10}}', '{"result": { "$divide": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": -100, "c": -100}}', '{"result": { "$divide": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$divide": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "0.99999999999999"}}', '{"result": { "$divide": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "0.99999999999999999"}}', '{"result": { "$divide": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$divide": [ "$a", 5]}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [null, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [null, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": ["str", null]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$divide": ["$a", "$a.b"]}}');
SELECT * FROM bson_dollar_project('{"a":null, "b": 2}', '{"result": { "$divide": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$divide": [ "$a", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$divide": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$divide": [ "$a", "$a.b" ]}}');

-- Divide by zero is not allowed
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [2, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [2, {"$numberLong": "0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [2, {"$numberDecimal": "0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [2, 0.0]}}');
SELECT * FROM bson_dollar_project('{"a": 0.0}', '{"result": { "$divide": [2, "$a"]}}');

-- Number or args should be 2
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": {}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [1, 2, 3, 4, 5, 6]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [{"$divide": [1, 0]}]}}');

-- Only numbers should be allowed
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [1, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": ["str", 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [true, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$divide": [2, false]}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$divide": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$divide": ["$a", 5]}}');

-- $mod operator : basic 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [3, 2]}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": ["$a", 4]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": 10, "c": 10}}', '{"result": { "$mod": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"b": -100, "c": -100}}', '{"result": { "$mod": [ "$a.b", "$a.c"]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "10.2"}}', '{"result": { "$mod": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "0.99999999999999"}}', '{"result": { "$mod": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberDouble": "0.99999999999999999"}}', '{"result": { "$mod": [ "$a", {"$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "10"}}', '{"result": { "$mod": [ "$a", 5]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "500000000000"}}', '{"result": { "$mod": [ "$a", {"$numberDouble": "450000000000.0"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "500000000000"}}', '{"result": { "$mod": [ "$a", {"$numberLong": "450000000000"}]}}');
SELECT * FROM bson_dollar_project('{"a":{"$numberLong": "5"}}', '{"result": { "$mod": [ "$a", {"$numberDouble": "3"}]}}');
SELECT * FROM bson_dollar_project('{"a":5}', '{"result": { "$mod": [ "$a", {"$numberDouble": "3"}]}}');

-- $mod operator : Nan and Infinity
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [{ "$numberDouble" : "NaN" }, 2]}}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": ["$a", { "$numberDouble" : "NaN" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": ["$a", { "$numberDouble" : "Infinity" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDouble" : "NaN" }, { "$numberDouble" : "NaN" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDouble" : "Infinity" }, { "$numberDouble" : "Infinity" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDouble" : "Nan" }, { "$numberDouble" : "Infinity" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDouble" : "Infinity" }, { "$numberDouble" : "NaN" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDouble" : "Infinity" }, { "$numberDouble" : "Infinity" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDecimal" : "Infinity" }, { "$numberDecimal" : "Infinity" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDecimal" : "Infinity" }, { "$numberDecimal" : "Nan" }] }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [null, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [null, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": ["str", null]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": ["$a", "$a.b"]}}');
SELECT * FROM bson_dollar_project('{"a":null, "b": 2}', '{"result": { "$mod": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1, "b": 2}', '{"result": { "$mod": [ "$a", null ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$mod": [ "$a", "$b" ]}}');
SELECT * FROM bson_dollar_project('{"a":1 }', '{"result": { "$mod": [ "$a", "$a.b" ]}}');

-- $mod operator : can't mod by zero
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": ["$a", { "$numberDouble" : "0" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDecimal" : "Infinity" }, { "$numberDecimal" : "0" }] }}');
SELECT * FROM bson_dollar_project('{"a": 2}', '{"result": { "$mod": [{ "$numberDecimal" : "Infinity" }, 0] }}');

-- $mod operator : Only numerics allowed
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [1, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": ["str", 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [true, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$mod": [2, false]}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$mod": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$mod": ["$a", 5]}}');

-- $mod operator : Return types based on operands types
--  Any operand Decimal => Result is Decimal 
--  Any operand Long and the result has no fraction => Result is Long
--  Everything else => Result is Double
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberLong": "10"}, {"$numberLong": "-1"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberLong": "10"}, {"$numberDouble": "1.032"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberLong": "10"}, 21]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberLong": "10"}, {"$numberDecimal": "1.032"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDecimal": "10"}, {"$numberDecimal": "1.032"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDecimal": "10"}, {"$numberDouble": "1.032"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDecimal": "10"}, 21]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDouble": "10"}, {"$numberDouble": "1.032"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDouble": "10"}, {"$numberDouble": "7"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDouble": "10"}, 7]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDouble": "10"}, {"$numberLong": "-1"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ {"$numberDouble": "10.78"}, {"$numberLong": "-1"}]}}');
SELECT * FROM bson_dollar_project('{"a": 1}', '{"result": { "$mod": [ 89, 7]}}');

-- $abs: returns expected value
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberDouble": "-232232.23"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberLong": "-2323232"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberLong": "-9223372036854775807"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": -2147483648}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberDecimal": "-9223372036854775808"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{"a": -2}', '{"result": { "$abs": {"$add": [-2, "$a"]}}}');

-- $abs: null/undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$abs": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": null}}');

-- $abs: can't calculate abs of MIN_INT64
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": {"$numberLong": "-9223372036854775808"}}}');

-- $abs: supports only numerics
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": [{}]}}');

-- $abs: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$abs": [1, 2, 3, 4]}}');

-- $ceil: returns expected value
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberLong": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberLong": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDouble": "23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDouble": "-23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDecimal": "23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDecimal": "-23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDecimal": "0.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": {"$log": [10, 2]}}}');

-- $ceil: null/undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$ceil": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": null}}');

-- $ceil: supports only numerics
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": [{}]}}');

-- $ceil: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ceil": [1, 2, 3, 4]}}');

-- $exp: returns expected result
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": 10}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": {"$numberLong": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": {"$numberDecimal": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": {"$numberDecimal": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": {"$numberDecimal": "NaN"}}}');

-- $exp: null/undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": null}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$exp": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": "$a"}}');

-- $exp: input should be numeric 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": "str"}}');
SELECT * FROM bson_dollar_project('{"a": true}', '{"result": { "$exp": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": [[]]}}');

-- $exp: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": ["str", 1]}}');
SELECT * FROM bson_dollar_project('{"a": true}', '{"result": { "$exp": ["$a", 2 ,2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$exp": []}}');

-- $floor: returns expected value
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberLong": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberLong": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDouble": "23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDouble": "-23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDecimal": "23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDecimal": "-23064.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDecimal": "0.000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": {"$log": [10, 2]}}}');

-- $floor: null/undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$floor": "$a"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": null}}');

-- $floor: supports only numerics
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": [{}]}}');

-- $floor: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$floor": [1, 2, 3, 4]}}');

-- $ln: expected results
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDouble": "2.718281828459045"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDecimal": "2.718281828459045235360287471352662"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDouble": "0.0001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDecimal": "0.1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberLong": "1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{"$a": 1}', '{"result": { "$ln": "$a"}}');

-- $ln: null/undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": "$a"}}');
SELECT * FROM bson_dollar_project('{"$a": null}', '{"result": { "$ln": "$a"}}');

-- $ln: supports only numerics
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": [false]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": [{}]}}');

-- $ln: should be positive number greater than 0
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDouble": "-2"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberDecimal": "0"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": {"$numberLong": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": -100}}');

-- $ln: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$ln": [1, 2, 3, 4]}}');

-- $log: expected results
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [10, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "0.1"}, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDecimal": "10"}, {"$numberDouble": "10"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDecimal": "10"}, {"$numberDouble": "5"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "10"}, {"$numberDouble": "5"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberLong": "10"}, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberLong": "9223372036854775807"}, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "10"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "NaN"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "10"}, {"$numberDecimal": "NaN"}]}}');

-- $log: null/undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [10, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": ["$a", 10]}}');
SELECT * FROM bson_dollar_project('{"$a": null}', '{"result": { "$log": ["$a", 2]}}');

-- $log: number must be positive number greater than 0
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [-1, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [0, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDecimal": "0"}, 10]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [{"$numberDouble": "-3"}, 10]}}');

-- $log: base must be positive number greater than 1
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [1, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [2, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [2, { "$numberDecimal": "1"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [2, { "$numberDecimal": "0"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [2, { "$numberDouble": "0"}]}}');

-- $log: arguments must be numbers
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [[], 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [2, "str"]}}');

-- $log: takes exactly 2 arguments
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": {}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log": [1]}}');

-- $log10: expected results
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": 10}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberLong": "10"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "10"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDouble": "10"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "10000"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberLong": "10000"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "0.0000000001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDouble": "0.001"}}}');

-- $log10: null/undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$log10": "$a"}}');

-- $log10: argument must be numeric
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": [[]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": [{}]}}');

-- $log10: argument must be positive number
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberLong": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "-1"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDecimal": "0.000000000000"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": {"$numberDouble": "0.0000000"}}}');

-- $log10: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": [1, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$log10": []}}');

-- $pow: returns expected
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [-2, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [-2, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [-2, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberDouble": "2"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, { "$numberDouble": "31"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, { "$numberLong": "10"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, { "$numberLong": "62"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberLong": "10"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberDecimal": "10"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [5, { "$numberDecimal": "-112"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [1, { "$numberDecimal": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [1, { "$numberDouble": "NaN"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberDecimal": "NaN"}, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberDouble": "NaN"}, 2]}}');
-- should lose precision only when a double is returned
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [3, 35]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [3, {"$numberDecimal": "35"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [3, {"$numberDouble": "35"}]}}');

-- $pow: should coerce when there is overflow
-- int32 -> int64
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, 31]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, 62]}}');
-- int32 -> double
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, 63]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, 66]}}');
-- int64 -> double
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberLong": "2" }, 63]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberLong": "2" }, 64]}}');

-- $pow: should return double for negative exponent with bases not [-1, 0, 1].
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, { "$numberLong": "-2" }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [-2, { "$numberLong": "-2" }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [-4, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberLong": "-1" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [{ "$numberLong": "1" }, -2]}}');

-- $pow: null/undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [null, 31]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, "$a"]}}');

-- $pow: arguments must be numeric
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [[], 31]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, "a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [2, true]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [false, true]}}');

-- $pow: exponent can't be negative if base is 0
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0, -31]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0, -0.00000001]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0, {"$numberDecimal": "-0.00000001"}]}}');

-- $pow: takes exactly 2 arguments
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0, -31, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$pow": [1,2,3,4,5,6]}}');

-- $sqrt: returns expected
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": 4}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberLong": "4"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberLong": "9223372036854775807"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberDouble": "4"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberDecimal": "4"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {"$numberDecimal": "NaN"}}}');

-- $sqrt: null/undefined returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": "$a"}}');
SELECT * FROM bson_dollar_project('{"a": null}', '{"result": { "$sqrt": "$a"}}');

-- $sqrt: arg must be numeric
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": [[]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": {}}}');

-- $sqrt: arg must be greater than or equal to 0
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": { "$numberDouble": "-1000000"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": { "$numberDouble": "-0.00001"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": { "$numberDecimal": "-0.000000000000001"}}}');

-- $sqrt: takes exactly 1 arg
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": []}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": [1, 2, 3, 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sqrt": [1, 2]}}');

-- $round: defaults to 0 if 1 arg is provided
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": { "$numberDouble": "1.3" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.3" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": { "$numberDouble": "1.51" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.51" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.49" }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.49" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": { "$numberDecimal": "1.68" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "1.68" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": 10023 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": { "$numberLong": "10023" }}}');

-- $round: double, long or int
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.32" }, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.32" }, 50]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "1.32" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberLong": "132" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberLong": "132" }, 20]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [132, 20]}}');

-- $round: positive precision, limit to 35 digits for number decimal
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "123.391" }, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "123.391" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "123.391" }, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "123.397" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "1.397" }, 50]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "1.397" }, 35]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "132423423.397" }, 35]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "132423423.397" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "132423423.397" }, 15]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "-123.397" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "1.298912343250054252245154325" }, {"$numberDecimal": "20.000000"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "0.0" }, {"$numberDecimal": "100.00000"}]}}');

-- $round: negative precision
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "123.391" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "123.391" }, -3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "123.391" }, -4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "123.391" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "123.391" }, -4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberLong": "1234532345" }, -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberLong": "1234532345" }, -20]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [123402345, -8]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [123402345, -19]}}');

-- $round: NaN, Infinity/-Infinity, Null/Undefined
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "NaN" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "NaN" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDouble": "Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{ "$numberDecimal": "-Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [null, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [1232, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": ["$a", 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [343, "$a"]}}');

-- $round: overflow
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [2147483647, -1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{"$numberLong": "9223372036854775806"}, -1]}}');

-- $round: num args should be either 1 or 2.
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [2147483647, -1, 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [2147483647, -1, 4, 5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": []}}');

-- $round: args should be numeric
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [{}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [1, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [true, 1]}}');

-- $round: precision must be >= -20 and <= 100
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [214, -21]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [214, 101]}}');

-- $round: precision must be integral value in int64 range
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [214, {"$numberDecimal": "-21.1"} ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [214, {"$numberDouble": "-21.1"} ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$round": [214, {"$numberDecimal": "9223372036854775808"} ]}}');

-- $trunc: defaults to 0 if 1 arg is provided
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": { "$numberDouble": "1.3" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.3" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": { "$numberDouble": "1.51" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.51" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.49" }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.49" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": { "$numberDecimal": "1.68" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "1.68" }, 0]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": 10023 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": { "$numberLong": "10023" }}}');

-- $trunc: double, long or int
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.32" }, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.32" }, 50]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "1.32" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberLong": "132" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberLong": "132" }, 20]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [132, 20]}}');

-- $trunc: positive precision, limit to 35 digits for number decimal
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "123.391" }, 1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "123.391" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "123.391" }, 3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "123.397" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "1.397" }, 50]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "1.397" }, 35]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "132423423.397" }, 35]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "132423423.397" }, 100]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "132423423.397" }, 15]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "-123.397" }, 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "1.298912343250054252245154325" }, {"$numberDecimal": "20.000000"}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "0.0" }, {"$numberDecimal": "100.00000"}]}}');

-- $trunc: negative precision
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "123.391" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "123.391" }, -3]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "123.391" }, -4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "123.391" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "123.391" }, -4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberLong": "1234532345" }, -5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberLong": "1234532345" }, -20]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [123402345, -8]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [123402345, -19]}}');

-- $trunc: NaN, Infinity/-Infinity, Null/Undefined
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "NaN" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "NaN" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDouble": "Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{ "$numberDecimal": "-Infinity" }, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [null, -2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [1232, null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": ["$a", 2]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [343, "$a"]}}');

-- $trunc: num args should be either 1 or 2.
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [2147483647, -1, 4]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [2147483647, -1, 4, 5]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": []}}');

-- $trunc: args should be numeric
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [{}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [1, "str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [true, 1]}}');

-- $trunc: precision must be >= -20 and <= 100
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [214, -21]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [214, 101]}}');

-- $trunc: precision must be integral value in int64 range
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [214, {"$numberDecimal": "-21.1"} ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [214, {"$numberDouble": "-21.1"} ]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$trunc": [214, {"$numberDecimal": "9223372036854775808"} ]}}');

-- Test which verifies the infinity and NaN behavior
SELECT * FROM bson_dollar_project('{}', '{"result": { "$toInt": {"$numberDecimal": "inf"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$toInt": {"$numberDecimal": "-inf"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$toInt": {"$numberDecimal": "nan"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$toInt": {"$numberDecimal": "1e310"}}}'); -- This would have failed while converting to double earlier
