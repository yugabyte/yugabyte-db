SET citus.next_shard_id TO 1800000;
SET documentdb.next_collection_id TO 18000;
SET documentdb.next_collection_index_id TO 18000;

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1}, "name": "idx_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1.0}, "name": "idx_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1.1}, "name": "idx_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1}, "name": "idx_1", "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1}, "name": "idx_1", "unique": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"b": 1, "a": -1}, "name": "idx_1", "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"a": -1, "b": 1}, "name": "idx_2", "sparse": false}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "sparse": false, "wildcardProjection": {"a": 0, "b": {"c": false}}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "wildcardProjection": {"b.c": 0, "_id": 0, "a": false, "d": false}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "wildcardProjection": {"b.c": 0, "_id": 0, "d": false}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "unique": false, "wildcardProjection": {"b.c": 0, "_id": 0, "a": false}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "unique": false, "wildcardProjection": {"b.c": true, "a": 12}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_3", "partialFilterExpression": {}, "wildcardProjection": {"a": 0, "b": {"c": false}}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_5", "sparse": false, "wildcardProjection": {"a": true, "b": {"c": 1, "d": 1}, "b.e": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_5", "sparse": false, "wildcardProjection": {"b": {"e": 1, "d": 1, "c": 1}, "a": 5}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_5", "sparse": false, "wildcardProjection": {"a": true, "b": {"d": 1}, "b.e": 1}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_6", "sparse": false, "wildcardProjection": {"b": {"e": 1, "d": 1, "c": 1}, "a": 5}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"$and": [{"a": 1}, {"b": {"c": 1, "d": 1}}]}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"b": {"c": 1, "d": 1}, "a": 1}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_7", "partialFilterExpression": {"b": {"c": 1, "d": 1}, "a": 1}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"b": {"c": 1, "d": {"$eq": 1}}, "a": {"$eq": 1}}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"b": {"d": 1, "c": 1}, "a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"b": {"d": 1}, "b": {"c": 1}, "a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"b.d": 1, "b.c": 1, "a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_4", "partialFilterExpression": {"$and": [{"a": 1}, {"b": {"c": 1, "d": 2}}]}}]}', true);

-- none of below pairs are treated as same indexes

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_8", "partialFilterExpression": {"$or": [{"b": {"$eq": 1}}, {"b": {"$gt": 1}}]}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_8", "partialFilterExpression": {"b": {"$gte": 1}}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_9", "partialFilterExpression": {"b": {"$in": [1,2,3]}, "a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_9", "partialFilterExpression": {"b": {"$in": [2,1,3]}, "a": 1}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_10", "partialFilterExpression": {"b": {"$not": {"$in": [1,2,3]}}, "a": 1}}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"$**": 1}, "name": "idx_10", "partialFilterExpression": {"b": {"$nin": [1,2,3]}, "a": 1}}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_11", "unique": true, "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_11", "unique": false, "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_11", "unique": true, "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_11", "unique": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_11", "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_1", "indexes": [{"key": {"c": 1, "d": 2}, "name": "idx_11", "unique": true, "sparse": true}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_2", "indexes": [{"key": {"c": 1, "d": 2}, "name": "idx_1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_2", "indexes": [{"key": {"c": 1, "d": 2}, "name": "idx_2", "unique": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_2", "indexes": [{"key": {"c": 1, "d": 2}, "name": "idx_3", "sparse": true}]}', true);

CALL documentdb_api.drop_indexes('conflict_test', '{"dropIndexes": "collection_2", "index": {"c": 1, "d": 2}}');

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_3", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_1", "sparse": true}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_3", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_1", "sparse": true}, {"key": {"a": 1}, "name": "idx_2"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_3", "indexes": [{"key": {"b": 1}, "name": "idx_3"}, {"key": {"c": 1, "d": 1}, "name": "idx_1", "sparse": true}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_3", "indexes": [{"key": {"c": 1, "d": 1}, "name": "idx_1", "sparse": true}, {"key": {"a": 1}, "name": "idx_4"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_3", "indexes": [{"key": {"b": 1}, "name": "idx_4"}, {"key": {"c": 1, "d": 1}, "name": "idx_1", "sparse": true}]}', true);

-- Creating identical indexes via the same command is not allowed. ..
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_4", "indexes": [{"key": {"k": 1}, "name": "idx_1", "sparse": true}, {"key": {"k": 1}, "name": "idx_1", "unique": false, "sparse": true}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_4", "indexes": [{"key": {"k": 1}, "name": "idx_1", "sparse": true}]}', true);

-- .. However, if the index was created via a prior command; then
-- Mongo skips creating the identical index, even if it's specified
-- more than once.
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_4", "indexes": [{"key": {"k": 1}, "name": "idx_1", "sparse": true}, {"key": {"k": 1}, "name": "idx_1", "unique": false, "sparse": true}]}', true);

SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_4", "indexes": [{"key": {"k": 1}, "name": "idx_2", "unique": true}, {"key": {"k": 1}, "name": "idx_3", "unique": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('conflict_test', '{"createIndexes": "collection_4", "indexes": [{"key": {"k": 1}, "name": "idx_2", "unique": true}, {"key": {"k": 1}, "name": "idx_2", "unique": false}]}', true);
