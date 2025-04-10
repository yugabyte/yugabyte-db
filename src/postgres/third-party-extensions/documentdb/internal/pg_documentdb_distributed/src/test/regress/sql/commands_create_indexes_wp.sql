SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 1700000;
SET documentdb.next_collection_id TO 17000;
SET documentdb.next_collection_index_id TO 17000;

-- cannot specify wildcardProjection for a non-root wildcard index
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'wp_test',
  '{
     "createIndexes": "fail_test",
     "indexes": [
       {
         "key": {"a.$**": 1}, "name": "idx",
         "wildcardProjection": {"a": 1}
       }
     ]
   }',
   true
);

-- cannot specify wildcardProjection for a non-wildcard index
SELECT documentdb_api_internal.create_indexes_non_concurrently(
  'wp_test',
  '{
     "createIndexes": "fail_test",
     "indexes": [
       {
         "key": {"a": 1}, "name": "idx",
         "wildcardProjection": {"a": 1}
       }
     ]
   }',
   true
);

CREATE FUNCTION create_index_arg_using_wp(p_wp text)
RETURNS documentdb_core.bson
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
  )::documentdb_core.bson;
END;
$$ LANGUAGE plpgsql;

-- all fields specified in wildcardProjection must be included or excluded, except _id field
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": 0}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b.c": 0}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"b.c": 1, "a": 0}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"x": 1, "y.t": 0}, "b.c": 0}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"x": 1, "y": 1}, "b.c": 0}'), true);

-- wildcardProjection cannot be empty
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{}'), true);

-- wildcardProjection must be document
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('5'), true);

-- wildcardProjection cannot contain an empty document as an inner-level specification
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": {"c": 1, "d": {}, "e": 1}}'), true);

-- and inner-level specification must be a document or a path string
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "b": {"c": 1, "d": [], "e": 1}}'), true);

-- show that we throw an error for invalid paths used in wildcardProjection document
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a..b": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"a..b": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"a.b.": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": -1, "b": {"$aa": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"$a": -1, "b": {"aa": 1}}'), true);

-- idx_1: for _id field, we will take the last inclusion specification into the account
-- idx_2: not specifying inclusion for _id field would result in excluding _id field by default
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
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
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'ok_test_1', 'idx_1');
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'ok_test_1', 'idx_2');
SELECT documentdb_api.list_indexes_cursor_first_page('wp_test','{ "listIndexes": "ok_test_1" }') ORDER BY 1;

-- using $ in a field path is ok unless it's the first character
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_2",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": -1, "b": {"b.a$a.k$": 1}}
            }
        ]
    }',
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'ok_test_2', 'idx_1');

SELECT documentdb_api.list_indexes_cursor_first_page('wp_test','{ "listIndexes": "ok_test_2" }') ORDER BY 1;

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_3",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 1, "b": {"c": 1, "d.e": 1}, "_id": 0}
            }
        ]
    }',
    true
);

SELECT documentdb_distributed_test_helpers.drop_primary_key('wp_test', 'ok_test_3');

SELECT documentdb_api.list_indexes_cursor_first_page('wp_test','{ "listIndexes": "ok_test_3" }') ORDER BY 1;

SET citus.propagate_set_commands TO 'local';
BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a.b": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a": {"b": 1}}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"_id": 1}';

  -- cannot use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b.d": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b": {"d": 1}}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_3') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"e": 1}';
COMMIT;

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_4",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 0, "b": {"c": 0, "d.e": 0}, "f.g.h": 0}
            }
        ]
    }',
    true
);

SELECT documentdb_distributed_test_helpers.drop_primary_key('wp_test', 'ok_test_4');

SELECT documentdb_api.list_indexes_cursor_first_page('wp_test', '{ "listIndexes": "ok_test_4" }') ORDER BY 1;

SET citus.propagate_set_commands TO 'local';
BEGIN;
  set local enable_seqscan TO OFF;

  -- cannot use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a.b": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a": {"b": 1}}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"_id": 1}';

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b.d": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b": {"d": 1}}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"e": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"f.g": 1}';
COMMIT;

SELECT 1 FROM documentdb_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 1}, "a": {"k": 1}}');
SELECT 1 FROM documentdb_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 1}, "a": {"k": 2}}');
SELECT 1 FROM documentdb_api.insert_one('wp_test', 'ok_test_4', '{"b": {"d": 2}, "a": {"k": 2}}');

BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b.d": 1, "a.k": 1}';
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a.k": 1, "b.d": 1}';

  SELECT COUNT(*)=1 FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b.d": 1, "a.k": 1}';
  SELECT COUNT(*)=1 FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"a.k": 1, "b.d": 1}';
COMMIT;

BEGIN;
  set local enable_seqscan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"b.d": {"$in": [1,2,3]}}';

  -- cannot use idx_1 due to filter on "a.z.r" in "$or"
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"$or": [{"b.d": {"$eq": [1,2,3]}}, {"a.z": {"r": {"$gte": 5}}}]}';

  -- can use idx_1 since none of the quals in "$or" are excluded
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_4') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"$or": [{"b.d": {"$eq": [1,2,3]}}, {"k": 5}]}';
COMMIT;

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_5",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 0, "b": {"c": {"p": 0, "r": false}, "d._id": 0}, "_id": 1}
            }
        ]
    }',
    true
);

SELECT documentdb_api.list_indexes_cursor_first_page('wp_test','{ "listIndexes": "ok_test_5" }') ORDER BY 1;

SELECT documentdb_distributed_test_helpers.drop_primary_key('wp_test', 'ok_test_5');

SET citus.propagate_set_commands TO 'local';
BEGIN;
  set local enable_seqscan TO OFF;
  set local documentdb.forceRumIndexScantoBitmapHeapScan TO OFF;

  -- can use idx_1
  EXPLAIN (COSTS OFF) SELECT document FROM documentdb_api.collection('wp_test', 'ok_test_5') WHERE document OPERATOR(documentdb_api_catalog.@@) '{"d.e.f": 1, "_id": 0}';
COMMIT;

-- not the same index since this doesn't specify wildcardProjection
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "ok_test_5",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_2"
            }
        ]
    }',
    true
);

-- test path collision
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": 1, "a.b": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": 1, "a": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a.b.c": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c.d.e": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b": {"c.d": 1}, "a": {"b.c": {"d.e": 1}}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a.b.c": 1, "a.b": {"c.d": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c": 1}, "a.b": {"c.d": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c.d.e": 1}, "a.b": {"c.d": 1}}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test', create_index_arg_using_wp('{"a": {"b.c": {"d.e": 1}}, "a.b": {"c.d": 1}}'), true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_1",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": 1, "a": {"b": 1}}
            }
        ]
    }',
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'no_path_collision_1', 'idx_1');

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_2",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a": {"b": 1}, "a": 1}
            }
        ]
    }',
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'no_path_collision_2', 'idx_1');

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_3",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a.b": {"c.d": 1}, "a.b": 1}
            }
        ]
    }',
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'no_path_collision_3', 'idx_1');

SELECT documentdb_api_internal.create_indexes_non_concurrently('wp_test',
    '{
        "createIndexes": "no_path_collision_4",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a.b": 1, "a.b": {"c.d": 1}}
            }
        ]
    }',
    true
);
SELECT documentdb_distributed_test_helpers.mongo_index_get_pg_def('wp_test', 'no_path_collision_4', 'idx_1');
