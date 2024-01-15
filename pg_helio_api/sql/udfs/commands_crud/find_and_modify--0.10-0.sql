
/*
 * dbcommand.findAndModify, see
 * https://docs.mongodb.com/v5.0/reference/command/findAndModify/
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.find_and_modify(
    p_database_name text,
    p_message __CORE_SCHEMA__.bson,
    p_transaction_id text default NULL,
    p_result OUT __CORE_SCHEMA__.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_find_and_modify$$;
