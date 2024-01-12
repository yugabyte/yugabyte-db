
/*
 * helio_api.insert_one is a convenience function for inserting a single
 * single document.
 */
CREATE OR REPLACE FUNCTION helio_api.insert_one(
    p_database_name text,
    p_collection_name text,
    p_document helio_core.bson,
    p_transaction_id text default null)
RETURNS helio_core.bson
SET search_path TO helio_core, pg_catalog
AS $fn$
DECLARE
    v_insert helio_core.bson;
    p_resultBson helio_core.bson;
BEGIN
	v_insert := ('{"insert":"' || p_collection_name || '" }')::bson;

    SELECT p_result INTO p_resultBson FROM helio_api.insert(
      p_database_name,
      v_insert,
      p_document::bsonsequence,
      p_transaction_id);
    RETURN p_resultBson;
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION helio_api.insert_one(text,text,helio_core.bson,text)
    IS 'insert one document into a Helio collection';
