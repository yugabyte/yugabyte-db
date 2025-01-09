SET search_path TO helio_core,helio_api,helio_api_catalog,helio_api_internal;
SET citus.next_shard_id TO 17771000;
SET helio_api.next_collection_id TO 177710;
SET helio_api.next_collection_index_id TO 177710;
set helio_api.enableSchemaValidation = true;

--------------------------------------Need $jsonSchema--------------------------------------
SELECT helio_api.create_collection_view('schema_validation_insertion', '{ "create": "col", "validator": {"$jsonSchema": {"bsonType": "object", "properties": {"a": {"bsonType": "int"}}}}, "validationLevel": "strict", "validationAction": "error"}');

SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col", "documents":[{"_id":"1", "a":1}]}');
-- required not supported yet, so this should be inserted
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col", "documents":[{"_id":"2", "b":1}]}');
-- type mismatch
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col", "documents":[{"_id":"3", "a":"hello"}]}');
-- batch insert
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col", "documents":[{"_id":"4", "a":2},{"_id":"5", "a":3}, {"_id":"6", "a":"tt"}]}');
-- 0 documents should be inserted
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col') ORDER BY shard_key_value, object_id;
-- set validationAction to warn
SELECT helio_api.coll_mod('schema_validation_insertion', 'col', '{"collMod":"col", "validationAction": "warn"}');
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col", "documents":[{"_id":"7", "a":"hello"}]}');
-- 1 document should be inserted
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col') ORDER BY shard_key_value, object_id;




---------------------------------------------Need top level operator-----------------------------------------------------
-- $expr
SELECT helio_api.create_collection_view('schema_validation_insertion', '{ "create": "col1", "validator": { "$expr": {"$eq": [ "$a", "$b" ] } } }');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col1", "documents":[{"_id":"1", "a":1, "b":1, "c":1}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col1", "documents":[{"_id":"2", "a":3, "b":1, "c":2}]}');

-- $and
SELECT helio_api.create_collection_view('schema_validation_insertion', '{ "create": "col2", "validator": { "$and": [ { "a": { "$gt": 2 } }, {"$jsonSchema": {"bsonType": "object", "properties": {"a": {"bsonType": "int", "maximum":5}}}} ] } }');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col2", "documents":[{"_id":"1", "a":4}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col2", "documents":[{"_id":"2", "a":1}]}');
-- expect to throw error as 6 > 5 (maximum)
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col2", "documents":[{"_id":"3", "a":6}]}');
set helio_api.enableBypassDocumentValidation = true;
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col2", "documents":[{"_id":"2", "a":1}], "bypassDocumentValidation": true}');

---------------------------------------------simple case-----------------------------------------------------
-- field 
SELECT helio_api.create_collection_view('schema_validation_insertion', '{ "create": "col3", "validator": {"a":{"$type":"int"}}}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col3", "documents":[{"_id":"1", "a":1}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col3", "documents":[{"_id":"2", "a":"hello"}]}');

--$merge
--todo - need to check
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col_", "documents":[{"_id":"1001","a":"world"}]}');
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col_", "documents":[{"_id":"1002","a":2}]}');
SELECT * FROM aggregate_cursor_first_page('schema_validation_insertion', '{ "aggregate": "col_", "pipeline": [ { "$match": { "a": { "$type": "string" }}}, {"$merge" : { "into": "col3" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT * FROM aggregate_cursor_first_page('schema_validation_insertion', '{ "aggregate": "col_", "pipeline": [ { "$match": { "a": { "$type": "int" }}}, {"$merge" : { "into": "col3" }} ], "cursor": { "batchSize": 1 } }', 4294967294);
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col3') ORDER BY shard_key_value, object_id;

-- sharded collection test
SELECT helio_api.shard_collection('schema_validation_insertion', 'col3', '{ "a": "hashed" }', false);
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col3", "documents":[{"_id":"1", "a":"hello"}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col3", "documents":[{"_id":"2", "a":5}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col3", "documents":[{"_id":"3", "a":2}, {"_id":"4", "a":3}, {"_id":"5", "a":4}, {"_id":"6", "a":"string"}]}');
-- 5 documents should be inserted
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col3') ORDER BY shard_key_value, object_id;
-- set validationAction to warn
SELECT helio_api.coll_mod('schema_validation_insertion', 'col3', '{"collMod":"col3", "validationAction": "warn"}');
SELECT helio_api.insert('schema_validation_insertion','{"insert":"col3", "documents":[{"_id":"7", "a":"hello"}]}');
-- 6 document should be inserted
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col3') ORDER BY shard_key_value, object_id;


---------------------------------------------update-----------------------------------------------------
-- sharded collection test
-- will succeed as validationAction is warn
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":1},"u":{"$set":{"a":"one"}}}]}');
-- set validation action to error
SELECT helio_api.coll_mod('schema_validation_insertion', 'col3', '{"collMod":"col3", "validationAction": "error"}');
-- should throw error
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":4},"u":{"$set":{"a":"four"}}}]}');
-- should succeed
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":3},"u":{"$set":{"a":300}}}]}');
-- upsert succeeded
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":"abc"},"u":{"$set":{"_id":500, "a":500}}, "upsert":true}]}');
-- upsert failed
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":"abc"},"u":{"$set":{"a":"abcd"}}, "upsert":true}]}');
-- should succeed with bypassDocumentValidation
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":4},"u":{"$set":{"a":"four"}}}], "bypassDocumentValidation": true}');

-- multiple updates
-- throw error as multi update is not allowed on sharded collection
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":2},"u":{"$set":{"a":200}}, "multi":true} ]}');
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col3');

-- will throw error as validationLevel is strict
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":"four"},"u":{"$set":{"a":"fourty"}}} ]}');
-- moderate case
SELECT helio_api.coll_mod('schema_validation_insertion', 'col3', '{"collMod":"col3", "validationLevel": "moderate"}');
-- will succeed as validationLevel is moderate
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":"four"},"u":{"$set":{"a":"fourty"}}} ]}');
SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col3');
-- batch update
SELECT helio_api.update('schema_validation_insertion', '{"update":"col3", "updates":[{"q":{"a":500},"u":{"$set":{"a":5000}}}, {"q":{"a":"four"},"u":{"$set":{"a":"fourty"}}}, {"q":{"a":6},"u":{"$set":{"a":600, "_id":600}}, "upsert": true}, {"q":{"a":"string"},"u":{"$set":{"a":"str"}}, "upsert":true} ]}');
 
--unsharded collection test
SELECT helio_api.create_collection_view('schema_validation_insertion', '{ "create": "col4", "validator": {"a":{"$type":"int"}}, "validationLevel": "strict", "validationAction": "warn"}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col4", "documents":[{"_id":"1", "a":1}, {"_id":"2", "a":2}, {"_id":"3", "a":3}]}');
SELECT helio_api.insert('schema_validation_insertion', '{"insert":"col4", "documents":[{"_id":"4", "a":"hello"}]}');
-- will succeed as validationAction is warn
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":1},"u":{"$set":{"a":"one"}}}]}');
SELECT helio_api.coll_mod('schema_validation_insertion', 'col4', '{"collMod":"col4", "validationAction": "error"}');
-- should throw error
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":2},"u":{"$set":{"a":"one"}}}]}');
-- should succeed
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":3},"u":{"$set":{"a":300}}}]}');
-- upsert succeeded
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":"abc"},"u":{"$set":{"_id":500, "a":500}}, "upsert":true}]}');
-- upsert failed
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":"abc"},"u":{"$set":{"a":"abcd"}}, "upsert":true}]}');
-- should succeed with bypassDocumentValidation
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":2},"u":{"$set":{"a":"one"}}}], "bypassDocumentValidation": true}');

-- multiple updates
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":"one"},"u":{"$set":{"a":200}}, "multi":true} ]}');
-- will throw error 
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":300},"u":{"$set":{"a":"th"}}, "multi":true} ]}');

-- will throw error as validationLevel is strict
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":"hello"},"u":{"$set":{"a":"world"}}} ]}');
-- moderate case
SELECT helio_api.coll_mod('schema_validation_insertion', 'col4', '{"collMod":"col4", "validationLevel": "moderate"}');
-- will succeed as validationLevel is moderate
SELECT helio_api.update('schema_validation_insertion', '{"update":"col4", "updates":[{"q":{"a":"hello"},"u":{"$set":{"a":"ten"}}} ]}');

SELECT shard_key_value, object_id, document from helio_api.collection('schema_validation_insertion','col4');
