SET search_path TO helio_api, helio_api_catalog,helio_core;
SET helio_api.next_collection_id TO 6000;
SET helio_api.next_collection_index_id TO 6000;

---- createIndexes - top level - parse error ----
SELECT helio_api_internal.create_indexes_non_concurrently('db', NULL);
SELECT helio_api_internal.create_indexes_non_concurrently(NULL, '{}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "unknown_field": []}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": null, "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": null}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": 5}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": []}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1"}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": 1}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": 1}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [1,2]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": 1}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": 1}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"unique": "1"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1}}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"unknown_field": "111"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "_name_", "unknown_field": "111"}]}');

---- createIndexes - indexes.key - parse error ----
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"": 1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1, "": -1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": 1, "b": -1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {".$**": 1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": "bad"}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": 0}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": ""}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**": {"a": 1}}, "name": "my_idx"}]}');

-- note that those are valid ..
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**foo": 1}, "name": "my_idx_1"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": 1}, "name": "my_idx_3"}]}',true);

-- valid sparse index
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse": 1}, "name": "my_sparse_idx1", "sparse": true}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse_num": 1}, "name": "my_sparse_num_idx1", "sparse": 1}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"asparse_num": 1}, "name": "my_non_sparse_num_idx1", "sparse": 0}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.double": 1}, "name": "my_sparse_double_idx1", "sparse": 0.2}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": null, "createIndexes": "collection_1", "indexes": [{"key": 0, "key": {"bsparse": 1}, "name": "my_non_sparse_idx1", "sparse": false}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"cs.$**": 1}, "name": "my_wildcard_non_sparse_idx1", "sparse": false}]}',true);

-- invalid sparse indexes
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": true}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.$**": 1}, "name": "my_wildcard_sparse_idx1", "sparse": "true"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"bs.a": 1}, "name": "my_sparse_with_pfe_idx", "sparse": true, "partialFilterExpression": { "rating": { "$gt": 5 } }}]}',true);

-- sparse can create index for same key with different sparse options
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_sparse_a_b_idx", "sparse": true}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx", "sparse": false}]}',true);

-- valid hash indexes
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": "hashed"}, "name": "my_idx_hashed"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": "hashed", "b": 1 }, "name": "my_idx_hashed_compound"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1, "b": "hashed" }, "name": "my_idx_hashed_compound_hash"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a$**": "hashed"}, "name": "my_idx_dollar_name_1"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.c$": "hashed"}, "name": "my_idx_dollar_name_2"}]}',true);

-- invalid hash indexes
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed" }, "unique": 1, "name": "invalid_hashed"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed", "c": "hashed" }, "name": "invalid_hashed"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": "hashed", "c": 1, "d": "hashed" }, "name": "invalid_hashed"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b.$**": "hashed" }, "name": "invalid_hashed"}]}',true);

-- can't create index on same key with same sparse options
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx1"}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx2", "sparse": false}]}',true);
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.sparse.b": 1}, "name": "my_non_sparse_a_b_idx3", "sparse": true}]}',true);

-- invalid index names
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1 }, "name": "*"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": 1 }, "name": "name\u0000field"}]}');

-- For the next test, show the commands that we internally execute to build
-- & clean-up the collection indexes.
SET client_min_messages TO DEBUG1;

-- Creating another index with the same name is not ok.
-- Note that we won't create other indexes too, even if it would be ok to create them in a separate command.
SELECT helio_api_internal.create_indexes_non_concurrently(
'db',
'{
    "createIndexes": "collection_1",
    "indexes": [
      {"key": {"x.y.z": 1}, "name": "valid_index_1"},
      {"key": {"c.d.e": 1}, "name": "my_idx_5"},
      {"key": {"x.y": 1}, "name": "valid_index_2"}
    ]
}', true);

RESET client_min_messages;

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM helio_test_helpers.get_collection_indexes('db', 'collection_1') ORDER BY collection_id, index_id;

-- also show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM helio_test_helpers.get_data_table_indexes('db', 'collection_1')
ORDER BY indexrelid;

-- Test invalid indexes are not left behind
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**foo": 1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$**.foo": 1}, "name": "my_idx"}]}');

SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": -1, "a.$**": 1}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {}, "name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"name": "my_idx"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**foo": 1}, "name": "my_idx_13"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"..": 1}, "name": "my_idx_12"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a..b.$**": 1}, "name": "my_idx_10"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a..b.foo": 1}, "name": "my_idx_11"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"$a": 1}, "name": "my_idx_12"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {".": 1}, "name": "my_idx_12"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$b": 1}, "name": "my_idx_12"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.$b.$**": 1}, "name": "my_idx_12"}]}');
SELECT helio_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "collection_1", "indexes": [{"key": {"a.": 1}, "name": "my_idx_12"}]}');

-- show that we didn't leave any invalid collection indexes behind
SELECT * FROM helio_test_helpers.get_collection_indexes('db', 'collection_1') ORDER BY collection_id, index_id;

-- also show that we didn't leave any invalid pg indexes behind
SELECT indexrelid::regclass, indisvalid, indisready
FROM helio_test_helpers.get_data_table_indexes('db', 'collection_1')
ORDER BY indexrelid;

-- ******* Wilcard projection indexes ******* --
-- cannot specify wildcardProjection for a non-root wildcard index
SELECT helio_api_internal.create_indexes_non_concurrently(
  'wp_test',
  '{
     "createIndexes": "fail_test",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "idx",
         "wildcardProjection": {"a": 1}
       }
     ]
   }'
);

-- cannot specify wildcardProjection for a non-wildcard index
SELECT helio_api_internal.create_indexes_non_concurrently(
  'wp_test',
  '{
     "createIndexes": "fail_test",
     "indexes": [
       {
         "key": {"a": 1}, "name": "idx",
         "wildcardProjection": {"a": 1}
       }
     ]
   }'
);

CREATE FUNCTION create_index_arg_using_wp(p_wp text)
RETURNS helio_core.bson
AS $$
BEGIN
	RETURN format(
    '{
        "createIndexes": "fail_test",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx",
                "wildcardProjection": %s
            }
        ]
    }',
    p_wp
  )::helio_core.bson;
END;
$$ LANGUAGE plpgsql;

-- all fields specified in wildcardProjection must be included or excluded, except _id field
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": 0}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b.c": 0}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"b.c": 1, "a": 0}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"x": 1, "y.t": 0}, "b.c": 0}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"x": 1, "y": 1}, "b.c": 0}'));

-- wildcardProjection cannot be empty
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{}'));

-- wildcardProjection must be document
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('5'));

-- wildcardProjection cannot contain an empty document as an inner-level specification
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": {"c": 1, "d": {}, "e": 1}}'));

-- and inner-level specification must be a document or a path string
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": {"c": 1, "d": [], "e": 1}}'));

-- show that we throw an error for invalid paths used in wildcardProjection document
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a..b": 1}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"a..b": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"a.b.": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"$aa": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"$a": -1, "b": {"aa": 1}}'));

-- idx_1: for _id field, we will take the last inclusion specification into the account
-- idx_2: not specifying inclusion for _id field would result in excluding _id field by default
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_1",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"_id": 0, "a": {"x": 10.5, "y": true}, "_id": false, "b.c": 1, "_id": -0.6}
            },
            {
                "key": {"$**": -1}, "name": "idx_2",
                "wildcardProjection": {"a": {"x": 10.5, "y": true, "z.a.b": -100}, "b.c": 1, "k": {"z.a.b": -100}}
            }
        ]
    }',
    TRUE
);

SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'ok_test_1', 'idx_1');
SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'ok_test_1', 'idx_2');
SELECT helio_test_helpers.get_collection_indexes('wp_test','ok_test_1');

-- using $ in a field path is ok unless it's the first character
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_2",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": -1, "b": {"b.a$a.k$": 1}}
            }
        ]
    }',
    TRUE
);
SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'ok_test_2', 'idx_1');

SELECT helio_test_helpers.get_collection_indexes('wp_test','ok_test_2');

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_3",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 1, "b": {"c": 1, "d.e": 1}, "_id": 0}
            }
        ]
    }',
    TRUE
);

SELECT helio_test_helpers.drop_primary_key('wp_test', 'ok_test_3');

SELECT helio_test_helpers.get_collection_indexes('wp_test','ok_test_3');

BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "a.b": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "a": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "a": { "$eq": {"b": 1} } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "a": { "_id": 1 } }, "projection": { "a": 1 } }');

  -- cannot use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "b.d": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "b": { "$eq": {"d": 1} } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_3", "filter": { "e": { "$eq": 1 } }, "projection": { "a": 1 } }');
COMMIT;

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_4",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 0, "b": {"c": 0, "d.e": 0}, "f.g.h": 0}
            }
        ]
    }',
    TRUE
);

SELECT helio_test_helpers.drop_primary_key('wp_test', 'ok_test_4');

SELECT helio_test_helpers.get_collection_indexes('wp_test','ok_test_4');

BEGIN;
  set local enable_seqscan TO OFF;

  -- cannot use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "a.b": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "a": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "a": { "$eq": {"b": 1} } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "a": { "_id": 1 } }, "projection": { "a": 1 } }');

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "b.d": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "b": { "$eq": {"d": 1} } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "e": { "$eq": 1 } }, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "f.g": { "$eq": 1 } }, "projection": { "a": 1 } }');
COMMIT;

SELECT helio_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 1}, "a": {"k": 1}}');
SELECT helio_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 1}, "a": {"k": 2}}');
SELECT helio_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 2}, "a": {"k": 2}}');

BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "$and": [{"b.d": { "$eq": 1 } }, {"a.k": {"$eq": 1}}]}, "projection": { "a": 1 } }');
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": { "$and": [{"a.k": { "$eq": 1 } }, {"b.d": {"$eq": 1}}]}, "projection": { "a": 1 } }');

  SELECT COUNT(*)=1 FROM helio_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(helio_api_catalog.@@) '{"b.d": 1, "a.k": 1}';
  SELECT COUNT(*)=1 FROM helio_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(helio_api_catalog.@@) '{"a.k": 1, "b.d": 1}';

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": {"b.d": {"$in": [1,2,3]}}, "projection": { "a": 1 } }');

  -- cannot use idx_1 due to filter on "a.z.r" in "$or"
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": {"$or": [{"b.d": {"$eq": [1,2,3]}}, {"a.z": {"r": {"$gte": 5}}}]}, "projection": { "a": 1 } }');

  -- can use idx_1 since none of the quals in "$or" are excluded
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": {"$or": [{"b.d": {"$eq": [1,2,3]}}, {"k": 5}]}, "projection": { "a": 1 } }');
COMMIT;

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_5",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 0, "b": {"c": {"p": 0, "r": false}, "d._id": 0}, "_id": 1}
            }
        ]
    }',
    TRUE
);

SELECT helio_test_helpers.drop_primary_key('wp_test', 'ok_test_5');
SELECT helio_test_helpers.get_collection_indexes('wp_test','ok_test_5') ORDER BY 1;


BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM bson_aggregation_find('wp_test', '{ "find": "ok_test_4", "filter": {"d.e.f": {"$eq": 1}, "_id": {"$eq": 0}}, "projection": { "a": 1 } }');
COMMIT;

-- not the same index since this doesn't specify wildcardProjection
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_5",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_2"
            }
        ]
    }',
    TRUE
);

-- test path collision
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "a.b": 1}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": 1, "a": 1}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a.b.c": 1}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c.d.e": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c": {"d.e": 1}}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b.c": 1, "a.b": {"c.d": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c": 1}, "a.b": {"c.d": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c.d.e": 1}, "a.b": {"c.d": 1}}'));
SELECT helio_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c": {"d.e": 1}}, "a.b": {"c.d": 1}}'));

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_1",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 1, "a": {"b": 1}}
            }
        ]
    }',
    TRUE
);

SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'no_path_collision_1', 'idx_1');

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_2",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": {"b": 1}, "a": 1}
            }
        ]
    }'
);
SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'no_path_collision_2', 'idx_1');

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_3",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a.b": {"c.d": 1}, "a.b": 1}
            }
        ]
    }'
);
SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'no_path_collision_3', 'idx_1');

SELECT helio_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_4",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a.b": 1, "a.b": {"c.d": 1}}
            }
        ]
    }'
);
SELECT helio_test_helpers.helio_index_get_pg_def('wp_test', 'no_path_collision_4', 'idx_1');