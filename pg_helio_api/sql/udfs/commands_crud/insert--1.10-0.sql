
/*
 * processes a MongoDB insert wire protocol command.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.insert(
    p_database_name text,
    p_insert __CORE_SCHEMA__.bson,
    p_insert_documents __CORE_SCHEMA__.bsonsequence default NULL,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert$$;
COMMENT ON FUNCTION __API_SCHEMA__.insert(text,__CORE_SCHEMA__.bson,__CORE_SCHEMA__.bsonsequence,text)
    IS 'inserts documents into a Mongo collection for a mongo wire protocol command';

/* Command: insert */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.insert_one(
    p_collection_id bigint,
    p_shard_key_value bigint,
    p_document __CORE_SCHEMA__.bson,
    p_transaction_id text)
 RETURNS bool
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_insert_one$$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL__.insert_one(bigint,bigint,__CORE_SCHEMA__.bson,text)
    IS 'internal command to insert one document into a Mongo collection';
