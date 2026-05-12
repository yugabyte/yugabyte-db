SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 2200000;
SET documentdb.next_collection_id TO 22000;
SET documentdb.next_collection_index_id TO 22000;

-- --Test 1 Compound index test --
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('compound_orders', 'comp_index', '{"total_price": 1,"shipping_distance":1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('compound_orders', 'comp_index1', '{"delivery_hours": 1,"customer_rating":1}'), true);
select documentdb_api.list_indexes_cursor_first_page('orderdb', '{ "listIndexes": "compound_orders" }') ORDER BY 1;

--Test 2 Descending Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('order_items', 'test1', '{"item_count": -1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('order_items', 'test2', '{"to_city": -1}'), true);
select documentdb_api.list_indexes_cursor_first_page('orderdb', '{ "listIndexes": "order_items" }') ORDER BY 1;

--Test 3 Ascending  Descending Index --
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('order_shipments', 'test1', '{"promo_code": 1}'), true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('orderdb', documentdb_distributed_test_helpers.generate_create_index_arg('order_shipments', 'test2', '{"order_status": -1}'), true);
select documentdb_api.list_indexes_cursor_first_page('orderdb', '{ "listIndexes": "order_shipments" }') ORDER BY 1;
