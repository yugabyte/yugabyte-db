SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 8900000;
SET documentdb.next_collection_id TO 8900;
SET documentdb.next_collection_index_id TO 8900;

-- $bitAnd $bitOr $bitXor $bitNot
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2,3] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1,2,3] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,3] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [1] } }');

select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2.1,3] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1.8,2,3] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,3.7] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [1.4] } }');

select *from bson_dollar_project('{}', '{"result": { "$bitAnd": [{"$numberDecimal": "1.18"},3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitOr": [{"$numberDecimal": "1.18"},3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitXor": [{"$numberDecimal": "1.18"},3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitNot": {"$numberDecimal": "1.18"} } }');

select *from bson_dollar_project('{}', '{"result": { "$bitAnd": [123456789012345678,3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitOr": [123456789012345678,3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitXor": [123456789012345678,3] } }');
select *from bson_dollar_project('{}', '{"result": { "$bitNot": 123456789012345678 } }');

select *from bson_dollar_project('{}', '{"result": { "$bitAnd": 1.3 } }');
select *from bson_dollar_project('{}', '{"result": { "$bitOr": 1.3 } }');
select *from bson_dollar_project('{}', '{"result": { "$bitXor": 1.3 } }');
select *from bson_dollar_project('{}', '{"result": { "$bitNot": 1.3 } }');

-- $bitAnd $bitOr $bitXor $bitNot, use $array.
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": "$array" } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": "$array" } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": "$array" } }');
select *from bson_dollar_project('{"array": [3]}', '{"result": { "$bitNot": "$array" } }');

select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": ["$array", 4] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": ["$array",5] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": ["$array",4] } }');
select *from bson_dollar_project('{"array": [3]}', '{"result": { "$bitNot": ["$array"] } }');

select *from bson_dollar_project('{"array": [1,2.1,3]}', '{"result": { "$bitAnd": "$array" } }');
select *from bson_dollar_project('{"array": [1.5,2,3]}', '{"result": { "$bitOr": "$array" } }');
select *from bson_dollar_project('{"array": [1,2,3.4]}', '{"result": { "$bitXor": "$array" } }');
select *from bson_dollar_project('{"array": [3.1]}', '{"result": { "$bitNot": "$array" } }');

-- $bitAnd $bitOr $bitXor $bitNot, null in array.
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [2,3,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [2,3,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [2,3,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [2,3,null] } }');

-- $bitAnd $bitOr $bitXor $bitNot, null array.
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [] } }');

-- $bitAnd $bitOr $bitXor $bitNot, nested array
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2,3,[1,2]] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1,2,3,[3,4]] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,3,[5,6]] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [[1]] } }');

-- $bitAnd $bitOr $bitXor $bitNot, dotted path tests
SELECT * FROM bson_dollar_project('{"array": {"b": 1}}', '{"result": { "$bitAnd": ["$array.b",3]}}');
SELECT * FROM bson_dollar_project('{"array": {"b": 1}}', '{"result": { "$bitOr": ["$array.b",3]}}');
SELECT * FROM bson_dollar_project('{"array": {"b": 1}}', '{"result": { "$bitXor": ["$array.b",3]}}');
SELECT * FROM bson_dollar_project('{"array": {"b": 1}}', '{"result": { "$bitNot": "$array.b"}}');

select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1,2,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,null] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": [null] } }');

select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2,"$undefined"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1,2,"$undefined"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,"$undefined"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": ["$undefined"] } }');

select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitAnd": [1,2,"$undefinedPath"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitOr": [1,2,"$undefinedPath"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitXor": [1,2,"$undefinedPath"] } }');
select *from bson_dollar_project('{"array": [1,2,3]}', '{"result": { "$bitNot": ["$undefinedPath"]} }');
