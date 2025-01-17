set search_path to helio_core,helio_api,helio_api_catalog,pg_catalog;
SET citus.next_shard_id TO 7600000;
SET helio_api.next_collection_id TO 7600;
SET helio_api.next_collection_index_id TO 7600;

-- The tests in this file ensure that schema provided is valid, and throws error on all invalid scenarios.
-- Test for validating documents against valid json schema is provided in separate test file.

-------------------------------------------------------------------------------
--                          Object Validators                                --
-------------------------------------------------------------------------------

------------------------ properties -------------------------------------------

-- Must be an object
SELECT bson_dollar_json_schema('{ "name":"pazu" }','{ "$jsonSchema": { "properties": "name" } }');

-- Each property must be an object
SELECT bson_dollar_json_schema('{ "name":"pazu" }','{ "$jsonSchema": { "properties": { "name":"pazu" } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "name":"pazu" }','{ "$jsonSchema": { "properties": { "name" : { } } } }');


-------------------------------------------------------------------------------
--                          Common Validators                                --
-------------------------------------------------------------------------------

------------------------ type -------------------------------------------------

-- Must be a string or array
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": 1 } } } }');

-- Must be a valid json type
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": "hello" } } } }');

-- Json type "integer" not supported (MongoDB limitation)
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": "integer" } } } }');

-- Array must not be empty
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": [ ] } } } }');

-- Array elements must be strings
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": [ 2 ] } } } }');

-- Array elements must not contain duplicate values
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": [ "string", "string" ] } } } }');

-- Valid case - string
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": "string" } } } }');

-- Valid case - array
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "type": ["string", "object"] } } } }');

------------------------ bsonType ---------------------------------------------

-- Must be a string or array
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": 1 } } } }');

-- Must be a valid json type
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": "hello" } } } }');

-- Bson type "integer" not supported (MongoDB limitation). "int" is supported
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": "integer" } } } }');

-- Array must not be empty
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": [ ] } } } }');

-- Array elements must be strings
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": [ 2 ] } } } }');

-- Array elements must not contain duplicate values
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": [ "string", "string" ] } } } }');

-- Valid case - string
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": "string" } } } }');

-- Valid case - array
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "bsonType": ["string", "object"] } } } }');


-------------------------------------------------------------------------------
--                          Numeric Validators                               --
-------------------------------------------------------------------------------

------------------------ multipleOf -------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": null } } } }');

-- Must be a non-zero
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": 0 } } } }');

-- Must not be NaN
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDecimal" : "NaN"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDecimal" : "-NaN"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDouble" : "NaN"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDouble" : "-NaN"} } } } }');

-- Must not be INF
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDecimal" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDecimal" : "-INF"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDouble" : "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDouble" : "-INF"} } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberInt": "1"} } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "multipleOf": {"$numberDouble": "2.2"} } } } }');

------------------------ maximum ----------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "maximum": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "maximum": null } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "maximum": {"$numberDouble": "99.9"} } } } }');

------------------------ exclusiveMaximum -------------------------------------

-- Must be a boolean
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMaximum": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMaximum": null } } } }');

-- if exclusiveMaximum is present, maximum must be present too
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMaximum": true } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMaximum": true, "maximum" : 99 } } } }');

------------------------ minimum ----------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "minimum": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "minimum": null } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "minimum": {"$numberDouble": "4.0"} } } } }');

------------------------ exclusiveMinimum -------------------------------------

-- Must be a boolean
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMinimum": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMinimum": null } } } }');

-- if exclusiveMaximum is present, maximum must be present too
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMinimum": true } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "age": 6 }', '{ "$jsonSchema": { "properties" : { "age" : { "exclusiveMinimum": true, "minimum" : 5 } } } }');


-------------------------------------------------------------------------------
--                          String Validators                                --
-------------------------------------------------------------------------------

------------------------ maxLength --------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": null } } } }');

-- Must be representable in 64 bits integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDouble": "9223372036854775809"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDecimal": "NaN"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDecimal": "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDecimal": "-INF"} } } } }');

-- Must be integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDouble": "10.3"} } } } }');

-- Must be positive integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDouble": "-10"} } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "maxLength": {"$numberDouble": "10"} } } } }');

------------------------ minLength --------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": null } } } }');

-- Must be representable in 64 bits integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDouble": "9223372036854775809"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDecimal": "NaN"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDecimal": "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDecimal": "-INF"} } } } }');

-- Must be integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDouble": "1.3"} } } } }');

-- Must be positive integer
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDouble": "-1"} } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "minLength": {"$numberDouble": "1"} } } } }');


------------------------ pattern ----------------------------------------------

-- Must be a string
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "pattern": 1 } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "pattern": null} } } }');

-- Must be a valid regex
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "pattern": "\\" } } } }');

-- Valid Schemas
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "pattern": "" } } } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "properties" : { "name" : { "pattern": "^[a-zA-Z_]*$" } } } }');

-------------------------------------------------------------------------------
--                          Array Validators                                 --
-------------------------------------------------------------------------------

------------------------ items ------------------------------------------------

-- Must be an object or an array of objects
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": 1 } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": null } } } }');

-- if its an array, it must contain all objects
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": [{ }, 1] } } } }');

-- Valid Schemas
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": [{ }, {"bsonType":"int"} ] } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": [] } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "items": { } } } } }');

------------------------ additionalItems --------------------------------------

-- Must be an object/bool
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "additionalItems": 1 } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "additionalItems": null } } } }');

-- Valid Schema - Object
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "additionalItems": { } } } } }');

-- Valid Schema - Bool
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "additionalItems": true } } } }');

------------------------ maxItems ---------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": null } } } }');

-- Must be representable in 64 bits integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "9223372036854775809"} } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "NaN"} } } } }');

-- Must be integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "10.3"} } } } }');

-- Must be positive integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "-10"} } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "maxItems": {"$numberDouble": "10"} } } } }');

------------------------ minItems ---------------------------------------------

-- Must be a number
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": null } } } }');

-- Must be representable in 64 bits integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "9223372036854775809"} } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "INF"} } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "NaN"} } } } }');

-- Must be integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "1.3"} } } } }');

-- Must be positive integer
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "-1"} } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "minItems": {"$numberDouble": "1"} } } } }');

------------------------ uniqueItems ------------------------------------------

-- Must be a bool
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "uniqueItems": "hello" } } } }');
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "uniqueItems": null } } } }');

-- Valid Schema
SELECT bson_dollar_json_schema('{ "hobbies" : ["run", 1, {"eat" : "treats"} ] }', '{ "$jsonSchema": { "properties" : { "hobbies" : { "uniqueItems": true } } } }');


-------------------------------------------------------------------------------
--                          Unsupported Keywords                             --
-------------------------------------------------------------------------------

SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "$ref" : "hello", "$id" : 2 } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "$schema" : "hello" } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "default" : "hello" } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "definitions" : "hello" } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "format" : "hello" } }');
SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "id" : "hello" } }');

-------------------------------------------------------------------------------
--                          Unknown Keywords                                 --
-------------------------------------------------------------------------------

SELECT bson_dollar_json_schema('{ "name":"pazu" }', '{ "$jsonSchema": { "hello" : "hello" } }');
