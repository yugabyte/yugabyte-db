
/*
 * helio_api.find_and_modify processes a Mongo wire protocol findAndModify command.
 */
CREATE OR REPLACE FUNCTION helio_api.find_and_modify(
    p_database_name text,
    p_message helio_core.bson,
    p_transaction_id text default NULL,
    p_result OUT helio_core.bson,
    p_success OUT boolean)
 RETURNS record
 LANGUAGE C
 VOLATILE
AS 'MODULE_PATHNAME', $$command_find_and_modify$$;
