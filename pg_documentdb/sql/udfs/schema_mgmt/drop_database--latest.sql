/* db.dropDatabase(writeConcern) */
/**
 * @ingroup schema_mgmt
 * @brief Drops an existing database from the DocumentDB system.
 *
 * @details This function removes the specified database and all collections within it. Optionally, 
 *          a write concern can be provided to control the behavior of the drop operation.
 *          The function performs a loop to drop each collection associated with the database before 
 *          removing the database itself.
 *
 * **Usage Examples:**
 * - Drop a database:
 *   ```sql
 *   SELECT documentdb_api.drop_database('hr_database');
 *   -- Returns:
 *   --
 *   ```
 *

 * @param[in] p_database_name Name of the target database to drop. Must not be NULL.
 * @param[in] p_write_concern (Optional) BSON object specifying the write concern for the operation.
 *                             Defaults to NULL.
 *
 * **Returns:**
 * - Void if the database is dropped successfully.
 * - An error if the operation fails.
 */
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
    IS 'drop a mongo database';