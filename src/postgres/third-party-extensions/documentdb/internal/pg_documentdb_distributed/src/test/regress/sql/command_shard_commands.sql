SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 756000;
SET documentdb.next_collection_id TO 7560;
SET documentdb.next_collection_index_id TO 7560;
ALTER SEQUENCE pg_dist_colocationid_seq RESTART WITH 7560;
set documentdb.recreate_retry_table_on_shard to on;


CREATE FUNCTION command_sharding_get_collectionInfo(dbname text DEFAULT 'comm_sh_coll', filterValue text DEFAULT '')
RETURNS SETOF documentdb_core.bson
AS $$
BEGIN
	RETURN QUERY WITH base AS (SELECT bson_dollar_project(bson_dollar_unwind(cursorpage, '$cursor.firstBatch'), '{ "cursor.firstBatch.idIndex": 0, "cursor.firstBatch.info.uuid": 0, "cursor.firstBatch.options": 0 }')::documentdb_core.bson AS doc FROM list_collections_cursor_first_page(dbname,
        FORMAT('{ "listCollections": 1, "filter": { %s }, "addDistributedMetadata": true }', filterValue)::documentdb_core.bson))
    SELECT doc FROM base ORDER BY bson_orderby(doc, '{ "cursor.firstBatch.name": 1 }');
END;
$$ LANGUAGE plpgsql;

SELECT documentdb_api.insert_one('comm_sh_coll', 'single_shard', '{ "_id": 1, "a": 1, "b": 2, "c": 3, "d": 4 }');
SELECT documentdb_api.insert_one('comm_sh_coll', 'comp_shard', '{ "_id": 1, "a": 1, "b": 2, "c": 3, "d": 4 }');

-- test new shard collection API - invalid inputs
SELECT documentdb_api.shard_collection('{ }');
SELECT documentdb_api.shard_collection('{ "key": { "_id": "hashed" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": 1 } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": 1 }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "numInitialChunks": -1 }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "numInitialChunks": 5678 }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "collation": { "a": "b" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "timeseries": { "a": "b" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.", "key": { "_id": "hashed" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": ".single_shard", "key": { "_id": "hashed" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": ".", "key": { "_id": "hashed" } }');
SELECT documentdb_api.shard_collection('{ "shardCollection": "admin.system.collections", "key": { "_id": "hashed" } }');

-- valid shard collection
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": false }');

-- cannot shard a sharded collection
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.single_shard", "key": { "a": "hashed" }, "unique": false }');

-- shard collection creates a new collection.
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.new_coll", "key": { "_id": "hashed" }, "unique": false }');

-- call listCollections and validate state.
SELECT command_sharding_get_collectionInfo();
SELECT * FROM public.citus_tables tbls JOIN
    (SELECT 'documentdb_data.documents_' || collection_id AS mongo_table_name FROM documentdb_api_catalog.collections WHERE database_name = 'comm_sh_coll'
    UNION ALL SELECT 'documentdb_data.retry_' || collection_id AS mongo_table_name FROM documentdb_api_catalog.collections WHERE database_name = 'comm_sh_coll') colls ON tbls.table_name::text = colls.mongo_table_name
    ORDER BY colocation_id ASC;


-- shard with different shard count
SELECT documentdb_api.shard_collection('{ "shardCollection": "comm_sh_coll.comp_shard", "key": { "_id": "hashed" }, "unique": false, "numInitialChunks": 3 }');
SELECT command_sharding_get_collectionInfo();

-- unshard - should return to the original colocationGroup.
SELECT documentdb_api.unshard_collection('{ "unshardCollection": "comm_sh_coll.comp_shard" }');

-- unsupported option
SELECT documentdb_api.unshard_collection('{ "unshardCollection": "comm_sh_coll.new_coll", "toShard": "shard2" }');
SELECT command_sharding_get_collectionInfo();

-- cannot unshard an unsharded collection.
SELECT documentdb_api.unshard_collection('{ "unshardCollection": "comm_sh_coll.comp_shard" }');

-- reshardCollection invalid apis
SELECT documentdb_api.reshard_collection('{ }');
SELECT documentdb_api.reshard_collection('{ "key": { "_id": "hashed" } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": 1 } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": 1 }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "numInitialChunks": 5678 }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "collation": { "a": "b" } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "numInitialChunks": -1 }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.", "key": { "_id": "hashed" } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": ".single_shard", "key": { "_id": "hashed" } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": ".", "key": { "_id": "hashed" } }');
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "admin.system.collections", "key": { "_id": "hashed" } }');

-- cannot reshard unsharded collection
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.comp_shard", "key": { "_id": "hashed" }, "unique": false }');

-- should noop since options are the same
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": false }');
SELECT command_sharding_get_collectionInfo();

-- with force, should redo
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": false, "forceRedistribution": true }');
SELECT command_sharding_get_collectionInfo();

-- reshard with new key allowed
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "a": "hashed" }, "unique": false, "numInitialChunks": 5 }');
SELECT command_sharding_get_collectionInfo();

-- reshard with same key and change chunks allowed
SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "a": "hashed" }, "unique": false, "numInitialChunks": 2, "forceRedistribution": true }');
SELECT command_sharding_get_collectionInfo();