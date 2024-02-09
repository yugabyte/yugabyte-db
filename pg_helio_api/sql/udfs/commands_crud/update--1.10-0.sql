
/*
 * __API_SCHEMA__.update processes a MongoDB update command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.update(
    p_database_name text,
    p_update __CORE_SCHEMA__.bson,
    p_insert_documents __CORE_SCHEMA__.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_update$$;
COMMENT ON FUNCTION __API_SCHEMA__.update(text,__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bsonsequence,text)
    IS 'update documents in a Mongo collection';


/* Command: update */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.update_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_query __CORE_SCHEMA__.bson,
    p_update __CORE_SCHEMA__.bson,
    p_shard_key __CORE_SCHEMA__.bson,
    p_is_upsert bool,
    p_sort __CORE_SCHEMA__.bson,

    /*
     * p_return_old_or_new: see update.c/UpdateReturnValue enum:
     *
     * NULL -> do not return
     * false -> return old document
     * true -> return new document
     */
    p_return_old_or_new bool,

    p_return_fields __CORE_SCHEMA__.bson,
    p_array_filters __CORE_SCHEMA__.bson,
    p_transaction_id text,
	OUT o_is_row_updated bool,
    OUT o_update_skipped bool,
	OUT o_is_retry bool,
	OUT o_reinsert_document __CORE_SCHEMA__.bson,
	OUT o_upserted_object_id bytea,
	OUT o_result_document __CORE_SCHEMA__.bson)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_update_one$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.update_one(bigint,bigint,__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson,bool,__CORE_SCHEMA__.bson,bool,__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bson,text)
    IS 'updates a single document in a Mongo collection';

/* Helper function for multi-update to track number of updated documents */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.bson_update_returned_value(shard_key_id bigint)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_update_returned_value$function$;