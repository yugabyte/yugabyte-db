/* db.dropDatabase(writeConcern) */
CREATE OR REPLACE FUNCTION helio_api.drop_database(
    p_database_name text,
    p_write_concern helio_core.bson default null)
RETURNS void
SET search_path TO helio_core, pg_catalog
AS $fn$
DECLARE
    v_collection record;
BEGIN
    /* TODO: lock on database to prevent concurrent table addition */
    FOR
      v_collection
    IN
      SELECT
        database_name, collection_name
      FROM
        __API_CATALOG_SCHEMA__.collections
      WHERE
        database_name = p_database_name
    LOOP
        PERFORM helio_api.drop_collection(v_collection.database_name, v_collection.collection_name);
    END LOOP;
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION helio_api.drop_database(text,helio_core.bson)
    IS 'drop a HelioAPI database';