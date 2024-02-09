
/*
 * helio_api.delete processes a Mongo wire protocol delete command.
 */
CREATE OR REPLACE FUNCTION helio_api.delete(
    p_database_name text,
    p_delete helio_core.bson,
    p_insert_documents helio_core.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT helio_core.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_delete$$;
COMMENT ON FUNCTION helio_api.delete(text,helio_core.bson,helio_core.bsonsequence,text)
    IS 'deletes documents from a Helio collection';

/* Command: delete */
CREATE OR REPLACE FUNCTION helio_api_internal.delete_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_query helio_core.bson,
    p_sort helio_core.bson,
    p_return_document bool,
    p_return_fields helio_core.bson,
    p_transaction_id text,
    OUT o_is_row_deleted bool,
    OUT o_result_deleted_document helio_core.bson)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_delete_one$$;
COMMENT ON FUNCTION helio_api_internal.delete_one(bigint,bigint,helio_core.bson,helio_core.bson,bool,helio_core.bson,text)
    IS 'deletes a single document from a Helio collection';
