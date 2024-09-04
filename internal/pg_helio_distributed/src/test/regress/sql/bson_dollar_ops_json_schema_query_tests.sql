set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7700000;
SET helio_api.next_collection_id TO 7700;
SET helio_api.next_collection_index_id TO 7700;


SELECT helio_api.insert_one('db', 'colJsonSchQry', '{ 
    "_id": 0, "vehicle": "car", "seats" : 4, "ac" : true, 
    "height" : 5.8, "width" : { "$numberDecimal": "4.2" }, 
    "model" : { "year" : 2020, "color" : "black" }, 
    "options": ["sunroof", {"drive": "FWD"}, {"drl": true, "free": true} ], 
    "engine": { "cc": 1500, "fuel": "petrol" }
}');


SELECT helio_api.insert_one('db', 'colJsonSchQry', '{ 
    "_id": 1, "vehicle": 20, "seats" : "many", "ac" : "1 ton", 
    "height" : "very high", "width" : { "upper" : 10, "lower" : 20 }, 
    "model" : 2010, 
    "options": null, 
    "engine": 1500
}');


-------------------------------------------------------------------------------
--                         Common Validations                                --
-------------------------------------------------------------------------------

---------------------------- "type" -------------------------------------------

-- All docs Match
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { } } }');

-- All docs Match, as none of docs have given field
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "cosmosdb" : { "type" : "string" } } } }');

-- No Match
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "type" : "boolean" } } } }');

-- Matches where "vehicle" is "string"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "type" : "string" } } } }');

-- Matches where "seats" is "number"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "seats" : { "type" : "number" } } } }');

-- Matches where "height" is "number"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "height" : { "type" : "number" } } } }');

-- Matches where "width" is "number"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "width" : { "type" : "number" } } } }');

-- Matches where "model" is "object"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "model" : { "type" : "object" } } } }');

-- Matches where "options" is "array"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "options" : { "type" : "array" } } } }');

-- Matches where "ac" is "boolean"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "ac" : { "type" : "boolean" } } } }');

-- Matches where "vehicle" is "string" and "seats" is "number"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "type" : "string" }, "seats" : { "type" : "number" } } } }');

-- Unsupported: "integer" type
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "seats" : { "type" : "integer" } } } }');


---------------------------- "bsonType" -------------------------------------------

-- All docs Match, as none of docs have given field
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "cosmosdb" : { "bsonType" : "string" } } } }');

-- No Match
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "bsonType" : "bool" } } } }');

-- Matches where "vehicle" is "string"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "bsonType" : "string" } } } }');

-- Matches where "seats" is "int"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "seats" : { "bsonType" : "int" } } } }');

-- Matches where "height" is "double"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "height" : { "bsonType" : "double" } } } }');

-- Matches where "width" is "decimal"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "width" : { "bsonType" : "decimal" } } } }');

-- Matches where "model" is "object"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "model" : { "bsonType" : "object" } } } }');

-- Matches where "options" is "array"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "options" : { "bsonType" : "array" } } } }');

-- Matches where "ac" is "boolean"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "ac" : { "bsonType" : "bool" } } } }');

-- Matches where "vehicle" is "string" and "seats" is "int"
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "vehicle" : { "bsonType" : "string" }, "seats" : { "bsonType" : "int" } } } }');

-- Unsupported: "integer" type
SELECT document FROM helio_api.collection('db', 'colJsonSchQry') WHERE helio_api_catalog.bson_dollar_json_schema(document,'{ "$jsonSchema": { "properties": { "seats" : { "type" : "integer" } } } }');

-------------------------------------------------------------------------------
--                         Numeric Validations                               --
-------------------------------------------------------------------------------

--------------------------- multipleOf ----------------------------------------

-- Doc is valid as field's value is a multiple of "multipleOf"
SELECT bson_dollar_json_schema('{"size": 0}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2 } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.4}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2.2 } } } }');

--In the following test doc is evaluated as invalid, because psql converts double value of 1.1 to 1.1000000000000001, which is not a multiple of 11
--This would not happen when req comes from gateway. So commenting out this test in psql, but will add this test as JS test in Gateway
--SELECT bson_dollar_json_schema('{"size": 11}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : {"$numberDouble" : "1.1"} } } } }');

-- Doc is invalid as field's value is not a multiple of "multipleOf"
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 3 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.4}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2.1 } } } }');

-- Doc is valid if field is not numeric
SELECT bson_dollar_json_schema('{"size": "hello"}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2 } } } }');
SELECT bson_dollar_json_schema('{"size": [5]}','{ "$jsonSchema": { "properties": { "size" : { "multipleOf" : 2 } } } }');

--------------------------- maximum ----------------------------------------

-- Doc is valid as field's value is less than or equal to given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 2}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"} } } } }');

-- Doc is invalid as field's value is more than the given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.2}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"} } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"} } } } }');

-- Doc is valid if field is not numeric
SELECT bson_dollar_json_schema('{"size": "hello"}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": [5]}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4 } } } }');

--------------------------- exclusiveMaximum -------------------------------

-- When "exclusiveMaximum" is true, these doc are valid as field's value is strictly less than the given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 3}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 3.99}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 3.99}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : true } } } }');

-- When "exclusiveMaximum" is true, these doc are invalid as field's value is more than or equal to the given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"}, "exclusiveMaximum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"}, "exclusiveMaximum" : true } } } }');

-- When "exclusiveMaximum" is false, these doc are valid as field's value is less than or equal to the given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 2}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"}, "exclusiveMaximum" : false } } } }');

-- When "exclusiveMaximum" is false, these doc are invalid as field's value is more than the given value of "maximum"
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.2}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4.1, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "-INF"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "NaN"}, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : {"$numberDecimal" : "INF"}, "exclusiveMaximum" : false } } } }');

-- Doc is valid if field is not numeric
SELECT bson_dollar_json_schema('{"size": "hello"}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": [5]}','{ "$jsonSchema": { "properties": { "size" : { "maximum" : 4, "exclusiveMaximum" : true } } } }');

--------------------------- minimum ----------------------------------------

-- Doc is valid as field's value is more than or equal to given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.2}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "NaN"} } } } }');

-- Doc is invalid as field's value is less than the given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 3}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 3.9}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1 } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.11 } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"} } } } }');

-- Doc is valid if field is not numeric
SELECT bson_dollar_json_schema('{"size": "hello"}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');
SELECT bson_dollar_json_schema('{"size": [5]}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4 } } } }');

--------------------------- exclusiveMinimum -------------------------------

-- When "exclusiveMinimum" is true, these doc are valid as field's value is strictly more than the given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4.11}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : true } } } }');

-- When "exclusiveMinimum" is true, these doc are invalid as field's value is less than or equal to the given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 3}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 3.99}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"}, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : true } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "NaN"}, "exclusiveMinimum" : true } } } }');

-- When "exclusiveMinimum" is false, these doc are valid as field's value is more than or equal to the given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.2}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 5}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "NaN"}, "exclusiveMinimum" : false } } } }');

-- When "exclusiveMinimum" is false, these doc are invalid as field's value is less than the given value of "minimum"
SELECT bson_dollar_json_schema('{"size": 3}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 3.9}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.1, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": 4.1}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4.11, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "-INF"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "INF"}, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": {"$numberDecimal" : "NaN"}}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : {"$numberDecimal" : "-INF"}, "exclusiveMinimum" : false } } } }');

-- Doc is valid if field is not numeric
SELECT bson_dollar_json_schema('{"size": "hello"}','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : false } } } }');
SELECT bson_dollar_json_schema('{"size": [3] }','{ "$jsonSchema": { "properties": { "size" : { "minimum" : 4, "exclusiveMinimum" : true } } } }');

-------------------------------------------------------------------------------
--                         String Validations                                --
-------------------------------------------------------------------------------

--------------------------- maxLength ----------------------------------------

-- Doc is valid as length of string is less than or eq to maxLength
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "maxLength" : 10 } } } }');
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "maxLength" : 4 } } } }');

-- Doc is invalid as length of string is less than or eq to maxLength
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "maxLength" : 2 } } } }');

-- Doc is valid if property is not a string
SELECT bson_dollar_json_schema('{"name": 2}','{ "$jsonSchema": { "properties": { "name" : { "maxLength" : 4 } } } }');

--------------------------- minLength ----------------------------------------

-- Doc is valid as length of string is more than or eq to minLength
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "minLength" : 2 } } } }');
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "minLength" : 4 } } } }');

-- Doc is invalid as length of string is more than the minLength
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "minLength" : 10 } } } }');

-- Doc is valid if property is not a string
SELECT bson_dollar_json_schema('{"name": 2}','{ "$jsonSchema": { "properties": { "name" : { "minLength" : 4 } } } }');

---------------------------- pattern -----------------------------------------

-- Doc is valid as given pattern matches the string
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "pattern" : "^P" } } } }');
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "pattern" : "[auzP]" } } } }');

-- Doc is invalid as given pattern does not matches the string
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "pattern" : "^a" } } } }');
SELECT bson_dollar_json_schema('{"name":"Pazu"}','{ "$jsonSchema": { "properties": { "name" : { "pattern" : "$z" } } } }');


-------------------------------------------------------------------------------
--                         Array Validations                                --
-------------------------------------------------------------------------------

---------------------------- items --------------------------------------------

-- Doc is valid as "data" array's each value matches the schema given in "items"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" } ] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}, { "type" : "boolean" }] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "minLength" : 1, "maxLength" : 10 }, { "type":"object" }, { "minimum" : 1, "maximum": 5 }] } } } }');

-- Doc is invalid as "data" array's each value does matches the schema given in "items"
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "object" }] } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"number" }] } } } }');

-- Doc is valid when "items" is given for Non-Array fields
SELECT bson_dollar_json_schema('{"data" : 2 }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" } ] } } } }');
SELECT bson_dollar_json_schema('{"data" : "Hello" }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "number" } ] } } } }');

---------------------------- additionalItems ----------------------------------

-- Doc is valid as "data" array values matches the schema given in "items", and data array has no more members than the number of members listed in "items"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : false } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}], "additionalItems" : false } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}, { "type" : "boolean" }], "additionalItems" : false } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "minLength" : 1, "maxLength" : 10 }, { "type":"object" }, { "minimum" : 1, "maximum": 5 }], "additionalItems" : false } } } }');

-- Doc is invalid as "data" array has more members than the number of members listed in "items"
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : false } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }], "additionalItems" : false } } } }');

-- Doc is valid as "data" array values matches the schema given in "items", while data array can have more members than number of members listed in "items"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }, { "type":"object" }, { "type": "number"}, { "type" : "boolean" }], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "minLength" : 1, "maxLength" : 10 }, { "type":"object" }, { "minimum" : 1, "maximum": 5 }], "additionalItems" : true } } } }');

-- Doc is valid, if "items" keyword is not provided, since "additionalItems" has no effect without "items"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "additionalItems" : false } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello" ] }','{ "$jsonSchema": { "properties": { "data" : { "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello" ] }','{ "$jsonSchema": { "properties": { "data" : { "additionalItems" : false } } } }');

-- Doc is valid, if "additionalItems" is provided for Non-Array fields
SELECT bson_dollar_json_schema('{"data" : 2 }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : 2 }','{ "$jsonSchema": { "properties": { "data" : { "items" : [{ "type" : "string" }], "additionalItems" : false } } } }');

---------------------------- maxItems -----------------------------------------

-- Doc is valid as "data" array members are less than or equal to given "maxItems"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 0 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 2 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello" ] }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 2 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1} ] }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 2 } } } }');

-- Doc is invalid as "data" array members are more than the given "maxItems"
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 2 } } } }');

-- Doc is valid when "maxItems" is given for Non-Array fields
SELECT bson_dollar_json_schema('{"data" : 2 }','{ "$jsonSchema": { "properties": { "data" : { "maxItems" : 2 } } } }');

---------------------------- minItems -----------------------------------------

-- Doc is valid as "data" array members are more than or equal to given "minItems"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 0 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1} ] }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 2 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello", {"a":1}, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 2 } } } }');

-- Doc is invalid as "data" array members are less than the given "minItems"
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 2 } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hello" ] }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 2 } } } }');

-- Doc is valid when "minItems" is given for Non-Array fields
SELECT bson_dollar_json_schema('{"data" : 2 }','{ "$jsonSchema": { "properties": { "data" : { "minItems" : 1 } } } }');

---------------------------- uniqueItems --------------------------------------

-- Doc is valid as all items in the given array are unique
SELECT bson_dollar_json_schema('{"data" : [ ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, 2 ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, 1.1 ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"1":1}, "1" ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"1":1}, "1", true ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"a":1}, {"b":1} ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"a":1, "b":1}, {"a":2, "b":2} ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"a":1, "b":1}, {"a":2, "b":2} ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');

-- Doc is invalid as all items in the given array are not unique
SELECT bson_dollar_json_schema('{"data" : [ 1, 1 ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, 1.0 ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, true, true ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ false, 1, false ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ "Hi", "Hi", 1 ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');

-- Doc is invalid, as objects that have same key-value pairs, even in different orders, are not unique. Applicable to objects recursively (i.e. objects in objects)
SELECT bson_dollar_json_schema('{"data" : [ 1, {"a":1, "b":2}, {"b":2, "a":1} ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
SELECT bson_dollar_json_schema('{"data" : [ 1, {"a": {"x":5, "y": 6}, "b":2}, {"b":2, "a": {"y":6, "x":5}} ] }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');

-- Doc is valid when "uniqueItems" is given for Non-Array fields
SELECT bson_dollar_json_schema('{"data" : 1 }','{ "$jsonSchema": { "properties": { "data" : { "uniqueItems" : true } } } }');
