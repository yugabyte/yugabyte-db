SET citus.next_shard_id TO 110000;
SET documentdb.next_collection_id TO 11000;
SET documentdb.next_collection_index_id TO 11000;

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;

---- createIndexes - top level - parse error ----
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', NULL, true);
SELECT documentdb_api_internal.create_indexes_non_concurrently(NULL, '{}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "unknown_field": []}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": null, "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": null}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": 5}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": []}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1"}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": 1}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": 1}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [1,2]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": 1}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": 1}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"unique": "1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"unknown_field": "111"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "_name_", "unknown_field": "111"}]}', true);

---- createIndexes - indexes.key - parse error ----
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"": 1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": 1, "b": -1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {".$**": 1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": "bad"}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": 0}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": ""}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": {"a": 1}}, "name": "my_idx"}]}', true);

-- note that those are valid ..
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**foo": 1}, "name": "my_idx_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": 1}, "name": "my_idx_3"}]}', true);

-- valid sparse index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse": 1}, "name": "my_sparse_idx1", "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse_num": 1}, "name": "my_sparse_num_idx1", "sparse": 1}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse_num": 1}, "name": "my_non_sparse_num_idx1", "sparse": 0}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.double": 1}, "name": "my_sparse_double_idx1", "sparse": 0.2}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": null, "createIndexes": "collection_1", "indexes": [{"key": 0, "key": {"bsparse": 1}, "name": "my_non_sparse_idx1", "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"cs.$**": 1}, "name": "my_wildcard_non_sparse_idx1", "sparse": false}]}', true);

-- invalid sparse indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": "true"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.a": 1}, "name": "my_sparse_with_pfe_idx", "sparse": true, "partialFilterExpression": { "rating": { "$gt": 5 } }}]}', true);

-- sparse can create index for same key with different sparse options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_sparse_a_b_idx", "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx", "sparse": false}]}', true);

-- valid hash indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": "hashed"}, "name": "my_idx_hashed"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": "hashed", "b": 1 }, "name": "my_idx_hashed_compound"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1, "b": "hashed" }, "name": "my_idx_hashed_compound_hash"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": "hashed"}, "name": "my_idx_dollar_name_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.c$": "hashed"}, "name": "my_idx_dollar_name_2"}]}', true);

-- invalid hash indexes
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed" }, "unique": 1, "name": "invalid_hashed"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed", "c": "hashed" }, "name": "invalid_hashed"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed", "c": 1, "d": "hashed" }, "name": "invalid_hashed"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b.$**": "hashed" }, "name": "invalid_hashed"}]}', true);

-- can't create index on same key with same sparse options
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx2", "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx3", "sparse": true}]}', true);

-- passing named args is also ok
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_database_name=>'db', p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"c.a$**": 1}, "name": "my_idx_4"}]}', p_skip_check_collection_create=>true);
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"d.a$**": 1}, "name": "my_idx_5"}]}', p_database_name=>'db', p_skip_check_collection_create=>true);

-- invalid index names
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1 }, "name": "*"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1 }, "name": "name\u0000field"}]}', true);

-- For the next test, show the commands that we internally execute to build
-- & clean-up the collection indexes.
SET client_min_messages TO DEBUG1;

-- Creating another index with the same name is not ok.
-- Note that we won't create other indexes too, even if it would be ok to create them in a separate command.
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  p_database_name=>'db',
  p_arg=>'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"x.y.z": 1}, "name": "valid_index_1"},
      {"key": {"c.d.e": 1}, "name": "my_idx_5"},
      {"key": {"x.y": 1}, "name": "valid_index_2"}
    ]
  }',
  p_skip_check_collection_create=>true
);

RESET client_min_messages;

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'collection_1') ORDER BY collection_id, index_id;

-- also show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM documentdb_distributed_test_helpers.get_data_table_indexes('db', 'collection_1')
ORDER BY indexrelid;

-- .., but those are not
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**foo": 1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**.foo": 1}, "name": "my_idx"}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": -1, "a.$**": 1}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {}, "name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "my_idx"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**foo": 1}, "name": "my_idx_13"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"..": 1}, "name": "my_idx_12"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a..b.$**": 1}, "name": "my_idx_10"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a..b.foo": 1}, "name": "my_idx_11"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"$a": 1}, "name": "my_idx_12"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {".": 1}, "name": "my_idx_12"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$b": 1}, "name": "my_idx_12"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$b.$**": 1}, "name": "my_idx_12"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.": 1}, "name": "my_idx_12"}]}', true);

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'collection_1') ORDER BY collection_id, index_id;

-- also show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM documentdb_distributed_test_helpers.get_data_table_indexes('db', 'collection_1')
ORDER BY indexrelid;

-- create a valid index.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14"}]}', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'collection_1') WHERE index_spec_as_bson OPERATOR(documentdb_api_catalog.@@) '{"name": "my_idx_14"}' ORDER BY collection_id, index_id;

-- creating the same index should noop
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14"}]}', true);
SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'collection_1')  WHERE index_spec_as_bson OPERATOR(documentdb_api_catalog.@@) '{"name": "my_idx_14"}' ORDER BY collection_id, index_id;

-- not the same index since this specifies partialFilterExpression
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"start_path": 1}, "name": "my_idx_14", "partialFilterExpression": {"a": 1}}]}', true);

-- valid mongo index type in specification, which are not supported yet
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": "2d"}, "name": "my_idx_2d"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": "2dsphere"}, "name": "my_idx_2dsphere"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": "text"}, "name": "my_idx_text"}]}', true);

-- test "v"
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": 2.1234, "name": "invalid_v"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": 10000000000000000, "name": "invalid_v"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": -10000000000000000, "name": "invalid_v"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": 100, "name": "invalid_v"}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": 2.0, "name": "valid_v"}]}', true);

-- same index since 2.0 == 2
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}, "v": 2, "name": "valid_v"}]}', true);

-- following are assumed to be identical to built-in _id index even if their names are different than "_id_"
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes" : [{"key": {"_id": 1}, "name" : "_id_1"}, {"key": {"_id": 1.0}, "name" : "_id_2"}]}', true);

-- but this is not identical to built-in _id index, so we will create it
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes" : [{"key": {"_id": 1.2}, "name" : "_id_3"}]}', true);

-- and this conflicts with _id_3 since key != {"_id": 1}, so we will throw an error
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes" : [{"key": {"_id": 1.2}, "name" : "_id_4"}]}', true);

-- background is ignored.
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"e.a": 1}, "name": "my_idx_6", "background": true }]}', p_database_name=>'db', p_skip_check_collection_create=>true);

-- hidden false is ignored.
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"e.b": 1}, "name": "my_idx_7", "hidden": false }]}', p_database_name=>'db', p_skip_check_collection_create=>true);

-- hidden true fails.
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"e.c": 1}, "name": "my_idx_8", "hidden": true }]}', p_database_name=>'db', p_skip_check_collection_create=>true);

-- ns is ignored.
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"e.f": 1}, "name": "my_idx_9", "ns": "foo.bar" }]}', p_database_name=>'db', p_skip_check_collection_create=>true);

-- readPreference is ignored
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "collection_1", "indexes": [{"key": {"e.g": 1}, "name": "my_idx_10" }], "$readPreference": { "mode": "secondary" }}', p_database_name=>'db', p_skip_check_collection_create=>true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "too_many_indexes", "indexes": [{"key": {"a0": 1}, "name": "a0"},{"key": {"a1": 1}, "name": "a1"},{"key": {"a2": 1}, "name": "a2"},{"key": {"a3": 1}, "name": "a3"},{"key": {"a4": 1}, "name": "a4"},{"key": {"a5": 1}, "name": "a5"},{"key": {"a6": 1}, "name": "a6"},{"key": {"a7": 1}, "name": "a7"},{"key": {"a8": 1}, "name": "a8"},{"key": {"a9": 1}, "name": "a9"},{"key": {"a10": 1}, "name": "a10"},{"key": {"a11": 1}, "name": "a11"},{"key": {"a12": 1}, "name": "a12"},{"key": {"a13": 1}, "name": "a13"},{"key": {"a14": 1}, "name": "a14"},{"key": {"a15": 1}, "name": "a15"},{"key": {"a16": 1}, "name": "a16"},{"key": {"a17": 1}, "name": "a17"},{"key": {"a18": 1}, "name": "a18"},{"key": {"a19": 1}, "name": "a19"},{"key": {"a20": 1}, "name": "a20"},{"key": {"a21": 1}, "name": "a21"},{"key": {"a22": 1}, "name": "a22"},{"key": {"a23": 1}, "name": "a23"},{"key": {"a24": 1}, "name": "a24"},{"key": {"a25": 1}, "name": "a25"},{"key": {"a26": 1}, "name": "a26"},{"key": {"a27": 1}, "name": "a27"},{"key": {"a28": 1}, "name": "a28"},{"key": {"a29": 1}, "name": "a29"},{"key": {"a30": 1}, "name": "a30"},{"key": {"a31": 1}, "name": "a31"},{"key": {"a32": 1}, "name": "a32"},{"key": {"a33": 1}, "name": "a33"},{"key": {"a34": 1}, "name": "a34"},{"key": {"a35": 1}, "name": "a35"},{"key": {"a36": 1}, "name": "a36"},{"key": {"a37": 1}, "name": "a37"},{"key": {"a38": 1}, "name": "a38"},{"key": {"a39": 1}, "name": "a39"},{"key": {"a40": 1}, "name": "a40"},{"key": {"a41": 1}, "name": "a41"},{"key": {"a42": 1}, "name": "a42"},{"key": {"a43": 1}, "name": "a43"},{"key": {"a44": 1}, "name": "a44"},{"key": {"a45": 1}, "name": "a45"},{"key": {"a46": 1}, "name": "a46"},{"key": {"a47": 1}, "name": "a47"},{"key": {"a48": 1}, "name": "a48"},{"key": {"a49": 1}, "name": "a49"},{"key": {"a50": 1}, "name": "a50"},{"key": {"a51": 1}, "name": "a51"},{"key": {"a52": 1}, "name": "a52"},{"key": {"a53": 1}, "name": "a53"},{"key": {"a54": 1}, "name": "a54"},{"key": {"a55": 1}, "name": "a55"},{"key": {"a56": 1}, "name": "a56"},{"key": {"a57": 1}, "name": "a57"},{"key": {"a58": 1}, "name": "a58"},{"key": {"a59": 1}, "name": "a59"},{"key": {"a60": 1}, "name": "a60"},{"key": {"a61": 1}, "name": "a61"},{"key": {"a62": 1}, "name": "a62"},{"key": {"a63": 1}, "name": "a63"}, {"key": {"a64": 1}, "name": "a64"}]}', true);

BEGIN;
  SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "too_many_indexes", "indexes": [{"key": {"a0": 1}, "name": "a0"},{"key": {"a1": 1}, "name": "a1"},{"key": {"a2": 1}, "name": "a2"},{"key": {"a3": 1}, "name": "a3"},{"key": {"a4": 1}, "name": "a4"},{"key": {"a5": 1}, "name": "a5"},{"key": {"a6": 1}, "name": "a6"},{"key": {"a7": 1}, "name": "a7"},{"key": {"a8": 1}, "name": "a8"},{"key": {"a9": 1}, "name": "a9"},{"key": {"a10": 1}, "name": "a10"},{"key": {"a11": 1}, "name": "a11"},{"key": {"a12": 1}, "name": "a12"},{"key": {"a13": 1}, "name": "a13"},{"key": {"a14": 1}, "name": "a14"},{"key": {"a15": 1}, "name": "a15"},{"key": {"a16": 1}, "name": "a16"},{"key": {"a17": 1}, "name": "a17"},{"key": {"a18": 1}, "name": "a18"},{"key": {"a19": 1}, "name": "a19"},{"key": {"a20": 1}, "name": "a20"},{"key": {"a21": 1}, "name": "a21"},{"key": {"a22": 1}, "name": "a22"},{"key": {"a23": 1}, "name": "a23"},{"key": {"a24": 1}, "name": "a24"},{"key": {"a25": 1}, "name": "a25"},{"key": {"a26": 1}, "name": "a26"},{"key": {"a27": 1}, "name": "a27"},{"key": {"a28": 1}, "name": "a28"},{"key": {"a29": 1}, "name": "a29"},{"key": {"a30": 1}, "name": "a30"},{"key": {"a31": 1}, "name": "a31"},{"key": {"a32": 1}, "name": "a32"},{"key": {"a33": 1}, "name": "a33"},{"key": {"a34": 1}, "name": "a34"},{"key": {"a35": 1}, "name": "a35"},{"key": {"a36": 1}, "name": "a36"},{"key": {"a37": 1}, "name": "a37"},{"key": {"a38": 1}, "name": "a38"},{"key": {"a39": 1}, "name": "a39"},{"key": {"a40": 1}, "name": "a40"},{"key": {"a41": 1}, "name": "a41"},{"key": {"a42": 1}, "name": "a42"},{"key": {"a43": 1}, "name": "a43"},{"key": {"a44": 1}, "name": "a44"},{"key": {"a45": 1}, "name": "a45"},{"key": {"a46": 1}, "name": "a46"},{"key": {"a47": 1}, "name": "a47"},{"key": {"a48": 1}, "name": "a48"},{"key": {"a49": 1}, "name": "a49"},{"key": {"a50": 1}, "name": "a50"},{"key": {"a51": 1}, "name": "a51"},{"key": {"a52": 1}, "name": "a52"},{"key": {"a53": 1}, "name": "a53"},{"key": {"a54": 1}, "name": "a54"},{"key": {"a55": 1}, "name": "a55"},{"key": {"a56": 1}, "name": "a56"},{"key": {"a57": 1}, "name": "a57"},{"key": {"a58": 1}, "name": "a58"},{"key": {"a59": 1}, "name": "a59"},{"key": {"a60": 1}, "name": "a60"},{"key": {"a61": 1}, "name": "a61"},{"key": {"a62": 1}, "name": "a62"},{"key": {"a63": 1}, "name": "a63"}, {"key": {"a64": 1}, "name": "a64"}]}', true);
ROLLBACK;

SELECT documentdb_api.create_collection('db', 'too_many_indexes');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "too_many_indexes", "indexes": [{"key": {"a0": 1}, "name": "a0"},{"key": {"a1": 1}, "name": "a1"},{"key": {"a2": 1}, "name": "a2"},{"key": {"a3": 1}, "name": "a3"},{"key": {"a4": 1}, "name": "a4"},{"key": {"a5": 1}, "name": "a5"},{"key": {"a6": 1}, "name": "a6"},{"key": {"a7": 1}, "name": "a7"},{"key": {"a8": 1}, "name": "a8"},{"key": {"a9": 1}, "name": "a9"},{"key": {"a10": 1}, "name": "a10"},{"key": {"a11": 1}, "name": "a11"},{"key": {"a12": 1}, "name": "a12"},{"key": {"a13": 1}, "name": "a13"},{"key": {"a14": 1}, "name": "a14"},{"key": {"a15": 1}, "name": "a15"},{"key": {"a16": 1}, "name": "a16"},{"key": {"a17": 1}, "name": "a17"},{"key": {"a18": 1}, "name": "a18"},{"key": {"a19": 1}, "name": "a19"},{"key": {"a20": 1}, "name": "a20"},{"key": {"a21": 1}, "name": "a21"},{"key": {"a22": 1}, "name": "a22"},{"key": {"a23": 1}, "name": "a23"},{"key": {"a24": 1}, "name": "a24"},{"key": {"a25": 1}, "name": "a25"},{"key": {"a26": 1}, "name": "a26"},{"key": {"a27": 1}, "name": "a27"},{"key": {"a28": 1}, "name": "a28"},{"key": {"a29": 1}, "name": "a29"},{"key": {"a30": 1}, "name": "a30"},{"key": {"a31": 1}, "name": "a31"},{"key": {"a32": 1}, "name": "a32"},{"key": {"a33": 1}, "name": "a33"},{"key": {"a34": 1}, "name": "a34"},{"key": {"a35": 1}, "name": "a35"},{"key": {"a36": 1}, "name": "a36"},{"key": {"a37": 1}, "name": "a37"},{"key": {"a38": 1}, "name": "a38"},{"key": {"a39": 1}, "name": "a39"},{"key": {"a40": 1}, "name": "a40"},{"key": {"a41": 1}, "name": "a41"},{"key": {"a42": 1}, "name": "a42"},{"key": {"a43": 1}, "name": "a43"},{"key": {"a44": 1}, "name": "a44"},{"key": {"a45": 1}, "name": "a45"},{"key": {"a46": 1}, "name": "a46"},{"key": {"a47": 1}, "name": "a47"},{"key": {"a48": 1}, "name": "a48"},{"key": {"a49": 1}, "name": "a49"},{"key": {"a50": 1}, "name": "a50"},{"key": {"a51": 1}, "name": "a51"},{"key": {"a52": 1}, "name": "a52"},{"key": {"a53": 1}, "name": "a53"},{"key": {"a54": 1}, "name": "a54"},{"key": {"a55": 1}, "name": "a55"},{"key": {"a56": 1}, "name": "a56"},{"key": {"a57": 1}, "name": "a57"},{"key": {"a58": 1}, "name": "a58"},{"key": {"a59": 1}, "name": "a59"},{"key": {"a60": 1}, "name": "a60"},{"key": {"a61": 1}, "name": "a61"},{"key": {"a62": 1}, "name": "a62"},{"key": {"a63": 1}, "name": "a63"}, {"key": {"a64": 1}, "name": "a64"}]}', true);

SET documentdb.enable_large_unique_index_keys TO false;

-- dropDups is ignored.
SELECT documentdb_api.insert_one('db','dropdups_ignore','{"_id": "1", "a": "dup" }', NULL);
SELECT documentdb_api.insert_one('db','dropdups_ignore','{"_id": "2", "a": "dup" }', NULL);
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "dropdups_ignore", "indexes": [{"key": {"a": 1}, "name": "dropdups_ignore_idx_1", "dropDups": true, "unique": true }]}', p_database_name=>'db', p_skip_check_collection_create=>true);
select documentdb_api.delete('db', '{"delete":"dropdups_ignore", "deletes":[{"q":{"_id": "2"}, "limit": 0 } ] }');
SELECT documentdb_api_internal.create_indexes_non_concurrently(p_arg=>'{"createIndexes": "dropdups_ignore", "indexes": [{"key": {"a": 1}, "name": "dropdups_ignore_idx_1", "dropDups": true, "unique": true }]}', p_database_name=>'db', p_skip_check_collection_create=>true);

SET documentdb.enable_large_unique_index_keys TO true;

-- tests with ignoreUnknownIndexOptions
-- -- invalid values of ignoreUnknownIndexOptions
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1"}], "ignoreUnknownIndexOptions": "hello" }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1"}], "ignoreUnknownIndexOptions": {"b":1} }', true);

-- -- ignoreUnknownIndexOptions of false (default value) will trigger error if there is any unknown field in index options. (unknown field is "dog" in these examples)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1", "dog": "pazu"}] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": false }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": 0.0 }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": null }', true);

-- -- ignoreUnknownIndexOptions of true will ignore any unknown field in index options. (unknown field is "dog" in these examples)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"a": 1}, "name": "a_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": true }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"b": 1}, "name": "b_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": 0.1 }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"c": 1}, "name": "c_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": -5.6 }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "indexColl_1", "indexes": [{"key": {"d": 1}, "name": "d_1", "dog": "pazu"}], "ignoreUnknownIndexOptions": 1 }', true);

-- index term options
-- we flow index term size option to the indexes that should truncate only
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexTermSizeLimit", "indexes": [ { "key" : { "a": 1 }, "name": "indexa"}] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexTermSizeLimit", "indexes": [ { "key" : { "$**": 1 }, "name": "indexwildcard"}] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexTermSizeLimit", "indexes": [ { "key" : { "a": 1, "b": 1 }, "name": "indexcompound"}] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "indexTermSizeLimit", "indexes": [ { "key" : { "$**": 1 }, "wildcardProjection": {"a": 0}, "name": "index_wildcard_projection"}] }', true);

WITH c1 AS (SELECT collection_id from documentdb_api_catalog.collections where collection_name = 'indexTermSizeLimit' and database_name = 'db')
SELECT indexdef FROM pg_indexes, c1 where tablename = 'documents' || '_' || c1.collection_id and schemaname = 'documentdb_data' ORDER BY indexname ASC;

SET documentdb.enable_large_unique_index_keys TO false;

-- for hashed, unique and text indexes we should not see the limit as those shouldn't be truncated
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "noIndexTermSizeLimit", "indexes": [ { "key": { "a": "text" }, "name": "a_text" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "noIndexTermSizeLimit", "indexes": [ { "key": { "a": "hashed" }, "name": "a_hashed" } ] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{ "createIndexes": "noIndexTermSizeLimit", "indexes": [ { "key": { "a": 1 }, "name": "a_unique", "unique": true } ] }', true);

SET documentdb.enable_large_unique_index_keys TO true;

WITH c1 AS (SELECT collection_id from documentdb_api_catalog.collections where collection_name = 'noIndexTermSizeLimit' and database_name = 'db')
SELECT indexdef FROM pg_indexes, c1 where tablename = 'documents' || '_' || c1.collection_id and schemaname = 'documentdb_data' ORDER BY indexname ASC;

SELECT ('{ "createIndexes": "indexColl_1", "indexes": [{ "key": { "description1": 1, "description2": 1, "description3": 1, "description4": 1, "description5": 1, "description6": 1, "description7": 1, "description8": 1, ' ||
 ' "description9": 1, "description10": 1, "description11": 1, "description12": 1, "description13": 1, "description14": 1, "description15": 1, "description16": 1, "description17": 1, ' ||
 ' "description18": 1, "description19": 1, "description20": 1 } ' || 
 ', "name": "description1_1_description2_1_description3_1_description4_1_description5_1_description6_1_description7_1_description8_1_description9_1_description10_1' ||
 '_description11_1_description12_1_description13_1_description14_1_description15_1_description16_1_description17_1_description18_1_description19_1_description20_1" } ]}')::bson;

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', 
 '{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "description1" : 1, "description2" : 1, "description3" : 1, "description4" : 1, "description5" : 1, "description6" : 1, "description7" : 1, "description8" : 1, "description9" : 1, "description10" : 1, "description11" : 1, "description12" : 1, "description13" : 1, "description14" : 1, "description15" : 1, "description16" : 1, "description17" : 1, "description18" : 1, "description19" : 1, "description20" : 1 }, "name" : "description1_1_description2_1_description3_1_description4_1_description5_1_description6_1_description7_1_description8_1_description9_1_description10_1_description11_1_description12_1_description13_1_description14_1_description15_1_description16_1_description17_1_description18_1_description19_1_description20_1" } ] }', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "%s" : 1 }, "name" : "%s_1" } ] }', repeat('a', 1200), repeat('a', 1200))::bson, true);

-- try 2 columns (super long)
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "%s" : 1, "b_%s": 1 }, "name" : "%s_1_b_%s_1" } ] }',
    repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200))::bson, true);

-- 4/5 super long columns
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1, "b_%s": 1, "c_%s": 1, "d_%s": 1 }, "name" : "a_b_c_d_%s_1" } ] }',
    repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200))::bson, true);

-- 10 super long columns
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1, "b_%s": 1, "c_%s": 1, "d_%s": 1, "e_%s": 1, "f_%s": 1, "g_%s": 1, "h_%s": 1, "i_%s": 1, "j_%s": 1 }, "name" : "a_b_c_d_e_f_g_h_i_j_%s_1" } ] }',
    repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200), repeat('a', 1200))::bson, true);

-- but 1 super long name fails
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1 }, "name" : "a_%s_1" } ] }',
    repeat('a', 1500), repeat('a', 1200))::bson, true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1, "b_%s": 1 }, "name" : "a_b_%s_1" } ] }',
    repeat('a', 1500), repeat('a', 1500), repeat('a', 1200))::bson, true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1, "b_%s": 1 }, "name" : "a_b_%s_1" } ] }',
    repeat('a', 1200), repeat('a', 1500), repeat('a', 1200))::bson, true);

-- however the name can be however long we want
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', FORMAT('{ "createIndexes" : "indexColl_1", "indexes" : [ { "key" : { "a_%s" : 1, "b_%s": 1 }, "name" : "a_b_%s_1" } ] }',
    repeat('a', 1200), repeat('a', 1200), repeat('a', 9000))::bson, true);

-- tests with blocking
-- -- invalid values of blocking
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"a": 1}, "name": "a_1"}], "blocking": "hello" }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"b": 1}, "name": "b_1"}], "blocking": {"b":1} }', true);

-- -- blocking of false (default value) will trigger create index concurrently.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"c": 1}, "name": "c_1"}] }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"d": 1}, "name": "d_1"}], "blocking": false }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"e": 1}, "name": "e_1"}], "blocking": 0.0 }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"f": 1}, "name": "f_1"}], "blocking": null }', true);

-- -- blocking of true will trigger create index non-concurrently.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"g": 1}, "name": "g_1"}], "blocking": true }', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"h": 1}, "name": "h_1"}], "blocking": 1.0 }', true);

-- Collection is not created in the same call
---- when blocking:true, we will let create index execute even if collection is not created in the call
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"i": 1}, "name": "i_1"}], "blocking": true }');
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "blocking_index", "indexes": [{"key": {"j": 1}, "name": "j_1"}], "blocking": false }');

SELECT * FROM documentdb_distributed_test_helpers.get_collection_indexes('db', 'blocking_index') ORDER BY collection_id, index_id;
