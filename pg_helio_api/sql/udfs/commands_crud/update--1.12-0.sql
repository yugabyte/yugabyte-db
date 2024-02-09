
/*
 * helio_api.update processes a Mongo update wire-protocol command.
 */
CREATE OR REPLACE FUNCTION helio_api.update(
    p_database_name text,
    p_update helio_core.bson,
    p_insert_documents helio_core.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT helio_core.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_update$$;
COMMENT ON FUNCTION helio_api.update(text,helio_core.bson,helio_core.bsonsequence,text)
    IS 'update documents in a helio collection';


/* Command: update */
CREATE OR REPLACE FUNCTION helio_api_internal.update_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_query helio_core.bson,
    p_update helio_core.bson,
    p_shard_key helio_core.bson,
    p_is_upsert bool,
    p_sort helio_core.bson,

    /*
     * p_return_old_or_new: see update.c/UpdateReturnValue enum:
     *
     * NULL -> do not return
     * false -> return old document
     * true -> return new document
     */
    p_return_old_or_new bool,

    p_return_fields helio_core.bson,
    p_array_filters helio_core.bson,
    p_transaction_id text,
	OUT o_is_row_updated bool,
    OUT o_update_skipped bool,
	OUT o_is_retry bool,
	OUT o_reinsert_document helio_core.bson,
	OUT o_upserted_object_id bytea,
	OUT o_result_document helio_core.bson)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_update_one$$;
COMMENT ON FUNCTION helio_api_internal.update_one(bigint,bigint,helio_core.bson,helio_core.bson,helio_core.bson,bool,helio_core.bson,bool,helio_core.bson,helio_core.bson,text)
    IS 'updates a single document in a Helio collection';

/* Helper function for multi-update to track number of updated documents */
CREATE OR REPLACE FUNCTION helio_api_internal.bson_update_returned_value(shard_key_id bigint)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_update_returned_value$function$;