/* db.dropDatabase(writeConcern) */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.drop_database(
    p_database_name text,
    p_write_concern __CORE_SCHEMA_V2__.bson default null)
RETURNS void
SET search_path TO __CORE_SCHEMA_V2__, pg_catalog
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
        PERFORM __API_SCHEMA_V2__.drop_collection(v_collection.database_name, v_collection.collection_name);
    END LOOP;
END;
$fn$ LANGUAGE plpgsql;
COMMENT ON FUNCTION __API_SCHEMA_V2__.drop_database(text,__CORE_SCHEMA_V2__.bson)
    IS 'drops a logical document database';