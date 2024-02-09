
/*
 * processes a Mongo insert wire protocol command.
 */
CREATE OR REPLACE FUNCTION helio_api.insert(
    p_database_name text,
    p_insert helio_core.bson,
    p_insert_documents helio_core.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT helio_core.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert$$;
COMMENT ON FUNCTION helio_api.insert(text,helio_core.bson,helio_core.bsonsequence,text)
    IS 'inserts documents into a Helio collection for a mongo wire protocol command';

/* Command: insert */
CREATE OR REPLACE FUNCTION helio_api_internal.insert_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_document helio_core.bson,
    p_transaction_id text)
 RETURNS bool
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert_one$$;
COMMENT ON FUNCTION helio_api_internal.insert_one(bigint,bigint,helio_core.bson,text)
    IS 'internal command to insert one document into a Helio collection';
