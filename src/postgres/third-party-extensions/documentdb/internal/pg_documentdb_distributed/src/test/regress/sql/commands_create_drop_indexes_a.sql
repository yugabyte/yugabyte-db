SET citus.next_shard_id TO 130000;
SET documentdb.next_collection_id TO 13000;
SET documentdb.next_collection_index_id TO 13000;

set search_path to documentdb_core,documentdb_api,documentdb_api_catalog;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "drop_all_test",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "my_idx_1"},
       {"key": {"b.$**": 1}, "name": "my_idx_2"}
     ]
   }',
   true
);

CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": ["*"]}');
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'drop_all_test') ORDER BY 1,2;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "drop_all_test",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "my_idx_1"},
       {"key": {"b.$**": 1}, "name": "my_idx_2"}
     ]
   }',
   true
);

-- Whatever rest of the arguments are, having "*" as the first one would drop
-- all indexes.
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": ["*", "_id_", "index_dne"]}');
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'drop_all_test') ORDER BY 1,2;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "drop_all_test",
     "indexes": [
       {"key": {"a.$**": 1}, "name": "my_idx_1"},
       {"key": {"b.$**": 1}, "name": "my_idx_2"}
     ]
   }',
   true
);

-- Given that "*" is not the first item of the array, we should interpret it
-- as a plain index name and throw an error complaining that index doesn't exist.
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": ["my_idx_1", "*"]}');
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'drop_all_test') ORDER BY 1,2;

-- cannot drop _id index
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": "_id_"}');
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": ["_id_", "*"]}');

CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_all_test", "index": "*"}');
SELECT * FROM documentdb_distributed_test_helpers.count_collection_indexes('db', 'drop_all_test') ORDER BY 1,2;

SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'db',
  '{
     "createIndexes": "drop_by_key",
     "indexes": [
       {"key": {"a": 1, "c.e": 1}, "name": "idx_1"},
       {"key": {"b.$**": 1}, "name": "idx_2"}
     ]
   }',
   true
);

-- no such index
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_by_key", "index": {"c.d": 1}}');

-- cannot drop _id index
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_by_key", "index": {"_id": 1}}');

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'drop_by_key') ORDER BY collection_id, index_id;

-- also show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM documentdb_distributed_test_helpers.get_data_table_indexes('db', 'drop_by_key')
ORDER BY indexrelid;

-- invalid key spec document but would just complain "no such index"
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_by_key", "index": {"$**.c.d": 1}}');

-- only drops "idx_2"
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_by_key", "index": "*",
                                                                     "index": {"a": 1, "c.e": 1},
                                                                     "index": {"b.$**": 1}}');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'drop_by_key') ORDER BY collection_id, index_id;

-- incorrect ordering of the (compound) keys, so cannot match "idx_1"
CALL documentdb_api.drop_indexes('db', '{"dropIndexes": "drop_by_key", "index": {"c.e": 1, "a": 1}}');

-- now drop "idx_1" too, but specify collection name using "deleteIndexes"
CALL documentdb_api.drop_indexes('db', '{"deleteIndexes": "drop_by_key", "index": {"a": 1, "c.e": 1}}');
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'drop_by_key') ORDER BY collection_id, index_id;

-- Show that having a record indicating an invalid collection index with the
-- same index name wouldn't cause any problems when creating the index.

SELECT documentdb_api.create_collection('db', 'collection_1000');

INSERT INTO documentdb_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
SELECT collection_id, 1000, ('idx_1', '{}', null, null, null, null, 2, null, null, null)::documentdb_api_catalog.index_spec_type, false FROM documentdb_api_catalog.collections
WHERE database_name = 'db' AND collection_name = 'collection_1000';

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1000", "indexes": [{"key": {"a": 1}, "name": "idx_1"}]}', true);

-- delete the fake invalid entry that we inserted above
DELETE FROM documentdb_api_catalog.collection_indexes
WHERE (index_spec).index_name = 'idx_1' AND
      index_id = 1000 AND
      (index_spec).index_key operator(documentdb_core.=) '{}' AND
      index_is_valid = false AND
      collection_id = (SELECT collection_id FROM documentdb_api_catalog.collections
                       WHERE database_name = 'db' AND collection_name = 'collection_1000');

-- Show that having a record indicating an invalid collection index with the
-- same index key wouldn't cause any problems when creating the index.

SELECT documentdb_api.create_collection('db', 'collection_1001');

INSERT INTO documentdb_api_catalog.collection_indexes (collection_id, index_id, index_spec, index_is_valid)
SELECT collection_id, 1000, ('idx_1', '{"a": 1}', null, null, null, null, 2, null, null, null)::documentdb_api_catalog.index_spec_type, false FROM documentdb_api_catalog.collections
WHERE database_name = 'db' AND collection_name = 'collection_1001';

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1001", "indexes": [{"key": {"a": 1}, "name": "idx_2"}]}', true);

-- delete the fake invalid entry that we inserted above
DELETE FROM documentdb_api_catalog.collection_indexes
WHERE (index_spec).index_name = 'idx_1' AND
      index_id = 1000 AND
      (index_spec).index_key operator(documentdb_core.=) '{"a": 1}' AND
      index_is_valid = false AND
      collection_id = (SELECT collection_id FROM documentdb_api_catalog.collections
                       WHERE database_name = 'db' AND collection_name = 'collection_1001');
