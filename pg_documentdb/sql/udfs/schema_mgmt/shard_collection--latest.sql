
/* mongo wire protocol command to shard or reshard a collection */
DROP FUNCTION IF EXISTS __API_SCHEMA_V2__.shard_collection CASCADE;
/**
 * @ingroup schema_mgmt
 * @brief Shards an existing collection in the DocumentDB database.
 *
 * @details The `shard_collection` function enables sharding for a specified collection within a target database. 
 *          It requires a shard key to distribute data across shards effectively. Optionally, additional 
 *          parameters such as unique constraint and collation can be provided to fine-tune the sharding behavior.
 *
 * **Usage Examples:**
 * - Shard an existing collection with a simple shard key:
 *   ```sql
 *   SELECT documentdb_api.shard_collection(
 *       'hr_database',
 *       'employees',
 *       '{"shardKey": {"employee_id": 1}}');
 *   -- Returns:
 *   -- 
 *   ```
 *
 * - Shard a collection with a unique shard key:
 *   ```sql
 *   SELECT documentdb_api.shard_collection(
 *       'hr_database',
 *       'employees',
 *       '{"shardKey": {"email": 1}, "unique": true}');
 *   -- Returns:
 *   -- 
 *   ```
 *
 * - Shard a collection with a specific collation:
 *   ```sql
 *   SELECT documentdb_api.shard_collection(
 *       'hr_database',
 *       'employees',
 *       '{"shardKey": {"last_name": 1}, "collation": {"locale": "en_US"}}');
 *   -- Returns:
 *   --
 *   ```

 * @param[in] p_database_name Name of the target database containing the collection to shard. Must not be NULL.
 * @param[in] p_collection_name Name of the collection to shard. Must not be NULL.
 * @param[in] p_shard_key BSON object specifying the shard key and optional sharding options. Must include the "shardKey" field.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.shard_collection(
    p_database_name text,
    p_collection_name text,
    p_shard_key __CORE_SCHEMA_V2__.bson,
    p_is_reshard bool default true)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;
 COMMENT ON FUNCTION __API_SCHEMA_V2__.shard_collection(text, text,__CORE_SCHEMA_V2__.bson,bool)
    IS 'Top level command to shard or reshard a collection';

-- create clones for the new schemas
/**
 * @brief Shards an existing collection in the DocumentDB database.
 * @param p_shard_key_spec BSON object specifying the shard key.
 * @return void
 * @ingroup schema_mgmt
 * **Usage Examples:**
 * ```SQL 
 * SELECT __API_SCHEMA_V2__.shard_collection('{ "shardKey": { "_id": "hashed" } }');
 * ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.shard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_shard_collection$$;

/**
 * @brief Reshards a specified collection.
 *
 * This function reshards the given collection based on the provided options.
 * If the same options are passed in as the current sharding configuration,
 * the function will skip the resharding process.
 *
 * @param reshardCollection The name of the collection to be resharded.
 * @param key The key to be used for sharding.
 * @param unique A boolean indicating whether the shard key should be unique.
 *
 * @return A notice indicating whether the resharing was skipped or performed.
 *
 * @ingroup schema_mgmt
 *
 * **Usage Examples:**
 * ```SQL 
 * SELECT documentdb_api.reshard_collection('{ "reshardCollection": "comm_sh_coll.single_shard", "key": { "_id": "hashed" }, "unique": false }');
 *  reshard_collection 
 *  ---------------------------------------------------------------------
 *  
 *  (1 row)
  * ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.reshard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_reshard_collection$$;

/**
 * @brief Unshards a specified collection.
 *
 * This function unshards the given collection based on the provided options.
 *
 * @param unshardCollection The name of the collection to be unsharded.
 *
 * @return A notice indicating the result of the unsharding operation.
 *
 * @ingroup schema_mgmt
 *
 * **Usage Examples:**
 * ```SQL 
 * SELECT documentdb_api.unshard_collection('{ "unshardCollection": "comm_sh_coll.comp_shard" }');
 *  unshard_collection 
 *  ---------------------------------------------------------------------
 *  
 *  (1 row)
 * ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.unshard_collection(
    p_shard_key_spec __CORE_SCHEMA_V2__.bson)
 RETURNS void
 LANGUAGE C
   STRICT
 AS 'MODULE_PATHNAME', $$command_unshard_collection$$;