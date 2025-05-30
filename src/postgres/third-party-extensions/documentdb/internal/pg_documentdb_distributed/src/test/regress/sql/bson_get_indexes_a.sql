set search_path to documentdb_core,documentdb_api,documentdb_api_catalog;
SET citus.next_shard_id TO 2100000;
SET documentdb.next_collection_id TO 21000;
SET documentdb.next_collection_index_id TO 21000;

--Test 1 Collection exist with only one Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'sal_index', '{"Salary": 1}'), true);
SELECT documentdb_api.list_indexes_cursor_first_page('msdb','{ "listIndexes": "employee" }') ORDER BY 1;

--Test 2 Collection exist with multiple Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'exp_index', '{"experience": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'age_index', '{"Age": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'gender_index', '{"Gender": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'designation_index', '{"Designation": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee', 'reports_to_index', '{"reports_to": 1}'), true);
SELECT documentdb_api.list_indexes_cursor_first_page('msdb','{ "listIndexes": "employee" }') ORDER BY 1;

--Test 3: Collection not exist --
SELECT documentdb_api.list_indexes_cursor_first_page('msdb','{ "listIndexes": "collection2" }') ORDER BY 1;

--Test 4: DB not exist --
SELECT documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "employee" }') ORDER BY 1;

--Test 5: DB and collection both does not exist --
SELECT documentdb_api.list_indexes_cursor_first_page('db','{ "listIndexes": "collection2" }') ORDER BY 1;

-- Test 6: Sparse is included in result only when specified:
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"a": 1}, "name": "my_sparse_idx1", "sparse": true}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"b": 1}, "name": "my_non_sparse_idx1", "sparse": false}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"c": 1}, "name": "my_idx1"}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"d": 1}, "name": "my_idx2", "sparse": 1.0}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"d": 1}, "name": "my_idx3", "sparse": 0.0}]}', true); 
SELECT documentdb_api_internal.create_indexes_non_concurrently('sparsedb', '{"createIndexes": "collection1", "indexes": [{"key": {"e": 1}, "name": "my_idx4", "sparse": 0.0, "expireAfterSeconds" : 3600}]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('sparsedb','{ "listIndexes": "collection1" }') ORDER BY 1;

-- Test 7: Unique indexes is included if it is specified and true.
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"f": 1}, "name": "my_idx3", "unique": 0.0}]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('uniquedb', '{"createIndexes": "collection1", "indexes": [{"key": {"g": 1}, "name": "my_idx4", "unique": 1.0, "sparse": 1.0, "expireAfterSeconds" : 5400}]}', true);
SELECT documentdb_api.list_indexes_cursor_first_page('uniquedb','{ "listIndexes": "collection1" }') ORDER BY 1;