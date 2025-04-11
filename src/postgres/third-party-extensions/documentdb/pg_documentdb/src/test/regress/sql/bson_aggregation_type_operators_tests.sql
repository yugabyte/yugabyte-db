SET search_path TO documentdb_api,documentdb_core;

SET documentdb.next_collection_id TO 101300;
SET documentdb.next_collection_index_id TO 101300;

--test int
select *from documentdb_api_catalog.bson_dollar_project('{"tests": 3}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 3 } }');

--test double
select *from documentdb_api_catalog.bson_dollar_project('{"tests": 3.1}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 3.1 } }');

--test string
select *from documentdb_api_catalog.bson_dollar_project('{"tests": "abc"}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": "abc" } }');

--test int64
select *from documentdb_api_catalog.bson_dollar_project('{"tests": 123456789012345678}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": 123456789012345678 } }');

--test array
select *from documentdb_api_catalog.bson_dollar_project('{"tests": [1, 2, 3.1]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [1, 2, 3.1] } }');

--test nested array
select *from documentdb_api_catalog.bson_dollar_project('{"tests": [1, 2, 3.1,[4, 5], 6]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [1, 2, 3.1,[4, 5], 6] } }');

--test nested object
select *from documentdb_api_catalog.bson_dollar_project('{"tests": [{"$numberDecimal": "1.2"},3]}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": [{"$numberDecimal": "1.2"},3] } }');

--test null
select *from documentdb_api_catalog.bson_dollar_project('{"tests": null}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": null } }');

--test NaN
select *from documentdb_api_catalog.bson_dollar_project('{"tests": {"$numberDouble": "NaN"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "NaN"} } }');

--test Infinity
select *from documentdb_api_catalog.bson_dollar_project('{"tests": {"$numberDouble": "Infinity"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "Infinity"} } }');

--test -Infinity
select *from documentdb_api_catalog.bson_dollar_project('{"tests": {"$numberDouble": "-Infinity"}}', '{"result": { "$toHashedIndexKey": "$tests" } }');
select *from documentdb_api_catalog.bson_dollar_project('{}', '{"result": { "$toHashedIndexKey": {"$numberDouble": "-Infinity"} } }');

--test path
select *from documentdb_api_catalog.bson_dollar_project('{"tests": {"test" : 5}}', '{"result": { "$toHashedIndexKey": "$tests.test" } }');
select *from documentdb_api_catalog.bson_dollar_project('{"tests": 3}', '{"result": { "$toHashedIndexKey": "$test" } }');
select *from documentdb_api_catalog.bson_dollar_project('{"tests": {"test" : 5}}', '{"result": { "$toHashedIndexKey": "$tests.tes" } }');