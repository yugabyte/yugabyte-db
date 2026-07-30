-- Based on or9.js
CREATE SCHEMA or9;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal,public,or9;
SET citus.next_shard_id TO 10000;
SET documentdb.next_collection_id TO 1000;
SET documentdb.next_collection_index_id TO 1000;

SELECT 1 FROM drop_collection('db','or9_test');
SELECT 1 FROM insert_one('db','or9_test', '{"field1":10,"field2":20}');

SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'or9_test');

-- Since we cannot define compound wildcard indexes, let's define two
-- different wildcard indexes here.
--
-- Indeed, our current implementation always creates a wildcard index, even for compound cases
-- but for compatibility, let's stick to a single-field wildcard index here.
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('or9_test', 'index_1', '{"field1.$**": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', documentdb_distributed_test_helpers.generate_create_index_arg('or9_test', 'index_2', '{"field2.$**": 1}'), true);

CREATE OR REPLACE FUNCTION or9.assert_count(expected_row_count int, query bson)
 RETURNS void
 LANGUAGE plpgsql
AS $$
DECLARE
	returned_row_count int;
BEGIN
	SELECT count(*) INTO returned_row_count
	FROM collection('db','or9_test') WHERE document @@ query;

	IF returned_row_count <> expected_row_count THEN
		RAISE 'query % returned % rows instead of %', query, returned_row_count, expected_row_count;
	END IF;
END;
$$;

BEGIN;
set local enable_seqscan TO OFF;

-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 5, "$lte": 15}}, {"field1": 10}]}');

SELECT assert_count(1, '{"$or": [{"field1": {"$gt": 15, "$lte": 25}}, {"field1": 10}]}');

SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 15, "$lte": 25}}, {"field2": 20}]}');
SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 20, "$lte": 25}}, {"field2": 20}]}');
SELECT assert_count(1, '{"$or": [{"field2": {"$gt": 25, "$lte": 35}}, {"field2": 20}]}');

-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 5, "$lte": 15}}, {"field1": 10, "field2": 20}]}');

SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 5, "$lte": 15}, "field2": 25}, {"field1": 10}]}');

SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 15, "$lte": 25}}, {"field2": 20, "field1": 10}]}');

SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 15, "$lte": 25}, "field1": 25}, {"field2": 20}]}');

SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 5, "$lte": 15}, "field2": 25}, {"field1": 10, "field2": 20}]}');
SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 20, "$lte": 25}, "field2": 25}, {"field1": 10, "field2": 20}]}');
-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"field1": {"$gte": 5, "$lte": 15}, "field2": 20}, {"field1": 10, "field2": 20}]}');

SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 15, "$lte": 25}, "field1": 25}, {"field1": 10, "field2": 20}]}');
SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 20, "$lte": 25}, "field1": 25}, {"field1": 10, "field2": 20}]}');
-- "SERVER-12594": there are two clauses in this case, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(1, '{"$or": [{"field2": {"$gte": 15, "$lte": 25}, "field1": 10}, {"field1": 10, "field2": 20}]}');

ROLLBACK;

DO $$
DECLARE
  v_collection_id bigint;
BEGIN
	SELECT collection_id INTO v_collection_id FROM documentdb_api_catalog.collections
  		WHERE database_name = 'db' AND collection_name = 'or9_test';
	EXECUTE format('TRUNCATE documentdb_data.documents_%s', v_collection_id);
END
$$;

SELECT 1 FROM insert_one('db','or9_test', '{"field1": 10, "field2": 50}');
SELECT 1 FROM insert_one('db','or9_test', '{"field1": 50, "field2": 10}');

BEGIN;
set local enable_seqscan to OFF;

-- "SERVER-12594": there are two clauses in the case below, because we do
-- not yet collapse OR of ANDs to a single ixscan.
SELECT assert_count(2, '{"$or": [{"field1": {"$in": [10, 50]}, "field2": {"$in": [10, 50]}}, {"field1": {"$in": [10, 50]}, "field2": {"$in": [10, 50]}}]}');

SELECT assert_count(2, '{"$or": [{"field1": {"$in": [10]}, "field2": {"$in": [10, 50]}}, {"field1": {"$in": [10, 50]}, "field2": {"$in": [10, 50]}}]}');
SELECT assert_count(2, '{"$or": [{"field1": {"$in": [10]}, "field2": {"$in": [10]}}, {"field1": {"$in": [10, 50]}, "field2": {"$in": [10, 50]}}]}');

ROLLBACK;