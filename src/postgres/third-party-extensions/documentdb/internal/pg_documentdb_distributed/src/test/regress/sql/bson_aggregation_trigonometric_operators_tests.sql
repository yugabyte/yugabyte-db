SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 7800000;
SET documentdb.next_collection_id TO 7800;
SET documentdb.next_collection_index_id TO 7800;

-- SET client_min_messages TO DEBUG3;

-- $degreesToRadians operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 45 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": -45 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 90 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 180 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 270 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": 1000 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": -1000 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDecimal": "90" } }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$degreesToRadians": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": null }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": "$a" }}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": {"$numberDouble": "NaN"} }}');

-- Infinity/-Infinity returns Infinity/-Infinity respectively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": {"$numberDouble": "Infinity"} }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": {"$numberDouble": "-Infinity"} }}');

-- Should error for non-number expressions.
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": "str" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": ["str"] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": true }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$degreesToRadians": false }}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$degreesToRadians": [2, "$a"] }}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$degreesToRadians": ["$a", 5] }}');


-- $radiansToDegrees operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees":  3.141592653589793}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": -3.141592653589793}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": 6.283185307179586}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": 12.566370614359172}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": { "$numberDecimal": "9.42477796076938" }}}');


-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": {"$numberDouble": "NaN"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions.
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$radiansToDegrees": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$radiansToDegrees": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$radiansToDegrees": ["$a", 5]}}');


-- $sin operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$sin": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$sin": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": {"$numberDouble": "NaN"}}}');

-- Should error if expression evaluates to Infinity/-Infinity
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sin": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$sin": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$sin": ["$a", 5]}}');


-- $cos operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$cos": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$cos": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": {"$numberDouble": "NaN"}}}');

-- Should error if expression evaluates to Infinity/-Infinity
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cos": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$cos": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$cos": ["$a", 5]}}');


-- $tan operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$tan": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$tan": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": {"$numberDouble": "NaN"}}}');

-- Should error if expression evaluates to Infinity/-Infinity
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tan": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$tan": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$tan": ["$a", 5]}}');


-- $sinh operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$sinh": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$sinh": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": {"$numberDouble": "NaN"}}}');

-- Infinity/-Infinity returns Infinity/-Infinity respectively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$sinh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$sinh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$sinh": ["$a", 5]}}');


-- $cosh operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$cosh": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$cosh": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": {"$numberDouble": "NaN"}}}');

-- Should error if expression evaluates to Infinity/-Infinity
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$cosh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$cosh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$cosh": ["$a", 5]}}');


-- $tanh operator
-- Radian argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": 1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": -1}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": 0}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$divide": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": 3.141592653589793 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$multiply": [3.141592653589793, 1.5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$multiply": [3.141592653589793, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": 100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": -100}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$numberDecimal": "1.5707963267948966" }}}');
SELECT * FROM bson_dollar_project('{"a": 4.71238898}', '{"result": { "$tanh": "$a"}}');

-- Degree argument
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": -45 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 0 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 90 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 180 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 270 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": 1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": -1000 }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDecimal": "1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDecimal": "-1e-10" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDecimal": "0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDecimal": "-0.0001" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDecimal": "90" } }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": { "$degreesToRadians": { "$numberDouble": "0.99999999999999" } }}}');
SELECT * FROM bson_dollar_project('{"a": 30}', '{"result": { "$tanh": { "$degreesToRadians": "$a" }}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": {"$numberDouble": "NaN"}}}');

-- Infinity/-Infinity returns Infinity/-Infinitytively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$tanh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$tanh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$tanh": ["$a", 5]}}');


-- $asin operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 0.7071067811865475 }}'); 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -0.7071067811865475 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 0.8660254037844386 }}'); 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -0.8660254037844386 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "0.1" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "-0.1" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -0.5 }}');
SELECT * FROM bson_dollar_project('{"a": 0.5}', '{"result": { "$asin": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -0.5}', '{"result": { "$asin": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": {"$numberDouble": "NaN"}}}');

-- Should error if expression lies outside [-1, 1]
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": -10.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asin": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$asin": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$asin": ["$a", 5]}}');


-- $acos operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 0.7071067811865475 }}'); 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -0.7071067811865475 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 0.8660254037844386 }}'); 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -0.8660254037844386 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "0.1" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "-0.1" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": { "$numberDecimal": "-0.0001" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -0.5 }}');
SELECT * FROM bson_dollar_project('{"a": 0.5}', '{"result": { "$acos": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -0.5}', '{"result": { "$acos": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": {"$numberDouble": "NaN"}}}');

-- Should error if expression lies outside [-1, 1]
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": -10.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acos": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$acos": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$acos": ["$a", 5]}}');


-- $atan operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": -0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": -10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": 100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": -100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$add": [10, 5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": { "$subtract": [10, 15] }}}');
SELECT * FROM bson_dollar_project('{"a": 0.5}', '{"result": { "$atan": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -0.5}', '{"result": { "$atan": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": 10}', '{"result": { "$atan": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -10}', '{"result": { "$atan": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": {"$numberDouble": "NaN"}}}');

-- Infinity/-Infinity returns Infinity/-Infinity respectively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$atan": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$atan": ["$a", 5]}}');


-- $atan2 operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-1, -1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [0, 0] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [0.9999999999, 0.9999999999] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-0.9999999999, -0.9999999999] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, 0] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-1, 0] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [0, 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [0, -1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [0.5, 0.5] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-0.5, -0.5] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [10, 10] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-10, -10] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [100, 100] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [-100, -100] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$divide": [1, 2]}, {"$divide": [1, 2] }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$multiply": [-0.5, 1]}, {"$multiply": [-0.5, 1] }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$add": [10, 5]}, {"$add": [10, 5] }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$subtract": [10, 15]}, {"$subtract": [10, 15] }]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$numberDecimal": "0.1" }, { "$numberDecimal": "0.2" }] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$numberDecimal": "-0.1" }, { "$numberDecimal": "-0.2" }] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$numberDecimal": "100" }, { "$numberDecimal": "-0.2" }] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$numberDouble": "0.1" }, { "$numberDouble": "0.2" }] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{ "$numberDouble": "-0.1" }, { "$numberDouble": "-0.2" }] }}');
SELECT * FROM bson_dollar_project('{"a": 0.5, "b": 0.5}', '{"result": { "$atan2": ["$a", "$b"] }}');
SELECT * FROM bson_dollar_project('{"a": -0.5, "b": -0.5}', '{"result": { "$atan2": ["$a", "$b"] }}');
SELECT * FROM bson_dollar_project('{"a": 10, "b": 10}', '{"result": { "$atan2": ["$a", "$b"] }}');
SELECT * FROM bson_dollar_project('{"a": -10, "b": -10}', '{"result": { "$atan2": ["$a", "$b"] }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, null] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": ["$a", null] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, "$a"] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, 50] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [50, 100] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, "$a"] }}');

-- NaN, <value> returns NaN. NaN, null returns null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [10, NaN] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [NaN, 10] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, NaN] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{"$numberDouble": "NaN"}, null] }}');

-- Infinity/-Infinity returns Infinity/-Infinity respectively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, {"$numberDouble": "Infinity"}] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{"$numberDouble": "Infinity"}, null] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [null, {"$numberDouble": "-Infinity"}] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [{"$numberDouble": "-Infinity"}, null] }}');

-- Should error for non-number expressions.
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": ["str", 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, "str"] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [true, 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, true] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [false, 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, false] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": ["$a", 1] }}');

-- Should error for wrong number of arguments
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, 1, 1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [] }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atan2": [1, 1, 1] }}');


-- $asinh operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": -0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": -10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": 100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": -100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$add": [10, 5] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": { "$subtract": [10, 15] }}}');
SELECT * FROM bson_dollar_project('{"a": 0.5}', '{"result": { "$asinh": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -0.5}', '{"result": { "$asinh": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": 10}', '{"result": { "$asinh": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -10}', '{"result": { "$asinh": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": {"$numberDouble": "NaN"}}}');

-- Infinity/-Infinity returns Infinity/-Infinity respectively
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$asinh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$asinh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$asinh": ["$a", 5]}}');


-- $acosh operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$add": [10, 5] }}}');
SELECT * FROM bson_dollar_project('{"a": 1.5}', '{"result": { "$acosh": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": 10}', '{"result": { "$acosh": "$a" }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": {"$numberDouble": "Infinity"}}}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": {"$numberDouble": "NaN"}}}');

-- Should error for values less than 1
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$subtract": [10, 15] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": -0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": -10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": {"$numberDouble": "-Infinity"}}}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$acosh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$acosh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$acosh": ["$a", 5]}}');


-- $atanh operator
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 0 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -1 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -0.9999999999 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": { "$numberDecimal": "1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": { "$numberDecimal": "-1e-10" }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -0.5 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": { "$divide": [1, 2] }}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": { "$multiply": [-0.5, 1] }}}');
SELECT * FROM bson_dollar_project('{"a": 0.5}', '{"result": { "$atanh": "$a" }}');
SELECT * FROM bson_dollar_project('{"a": -0.5}', '{"result": { "$atanh": "$a" }}');

-- Null or undefined should return null
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": null}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": "$a"}}');

-- NaN returns NaN
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": NaN}}');

-- Should error for values outside [-1, 1]
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": {"$numberDouble": "Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": {"$numberDouble": "-Infinity"}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -1.0000000001 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -10 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": 100 }}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": -100 }}');

-- Should error for non-number expressions. 
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": "str"}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": ["str"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": true}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$atanh": false}}');
SELECT * FROM bson_dollar_project('{"a": "str"}', '{"result": { "$atanh": [2, "$a"]}}');
SELECT * FROM bson_dollar_project('{"a": {}}', '{"result": { "$atanh": ["$a", 5]}}');
