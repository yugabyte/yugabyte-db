
/*
 * __API_SCHEMA__.insert_one is a convenience function for inserting a single
 * single document.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.insert_one(
    p_database_name text,
    p_collection_name text,
    p_document __CORE_SCHEMA__.bson,
    p_transaction_id text default null)
RETURNS __CORE_SCHEMA__.bson
SET search_path TO __CORE_SCHEMA__, pg_catalog
AS $fn$
DECLARE
    v_insert __CORE_SCHEMA__.bson;
    p_resultBson __CORE_SCHEMA__.bson;
BEGIN
	v_insert := ('{"insert":"' || p_collection_name || '" }')::bson;

    SELECT p_result INTO p_resultBson FROM __API_SCHEMA__.insert(
      p_database_name,
      v_insert,
      p_document::bsonsequence,
      p_transaction_id);
    RETURN p_resultBson;
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION __API_SCHEMA__.insert_one(text,text,__CORE_SCHEMA__.bson,text)
    IS 'insert one document into a Mongo collection';
