SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 2200000;
SET documentdb.next_collection_id TO 22000;
SET documentdb.next_collection_index_id TO 22000;

-- --Test 1 Compound index test --
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('Compound_test', 'comp_index', '{"compindex1": 1,"compindex2":1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('Compound_test', 'comp_index1', '{"compindex3": 1,"compindex4":1}'), true);
select documentdb_api.list_indexes_cursor_first_page('msdb', '{ "listIndexes": "Compound_test" }') ORDER BY 1;

--Test 2 Descending Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee2', 'test1', '{"col1": -1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee2', 'test2', '{"col2": -1}'), true);
select documentdb_api.list_indexes_cursor_first_page('msdb', '{ "listIndexes": "employee2" }') ORDER BY 1;

--Test 3 Ascending  Descending Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee3', 'test1', '{"col1": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('msdb', documentdb_distributed_test_helpers.generate_create_index_arg('employee3', 'test2', '{"col2": -1}'), true);
select documentdb_api.list_indexes_cursor_first_page('msdb', '{ "listIndexes": "employee3" }') ORDER BY 1;
