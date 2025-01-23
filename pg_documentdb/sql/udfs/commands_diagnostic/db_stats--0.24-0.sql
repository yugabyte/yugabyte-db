/**
 * @brief Returns storage statistics for a given database.
 *
 * This function returns storage statistics for the specified database, including information about collections, views, objects, and storage sizes.
 *
 * @param p_database_name The name of the database for which to retrieve statistics.
 * @param p_scale A scaling factor for the sizes (default is 1).
 * @param p_freeStorage A boolean indicating whether to include free storage in the statistics (default is false).
 *
 * @return BSON object containing the storage statistics for the database.
 *
 * @ingroup commands_diagnostic
 *
 * **Usage Examples:**
 * ```SQL 
 * SELECT documentdb_api.db_stats('db1');
 *  db_stats 
 *  ---------------------------------------------------------------------
 *  { "db" : "db1", "collections" : { "$numberLong" : "1" }, "views" : { "$numberLong" : "0" }, "objects" : { "$numberLong" : "21" }, "avgObjSize" : { "$numberDouble" : "30.0" }, "dataSize" : { "$numberDouble" : "630.0" }, "storageSize" : { "$numberDouble" : "16384.0" }, "indexes" : { "$numberLong" : "1" }, "indexSize" : { "$numberDouble" : "16384.0" }, "totalSize" : { "$numberDouble" : "32768.0" }, "scaleFactor" : { "$numberInt" : "1" }, "ok" : { "$numberInt" : "1" } }
 * (1 row)
 * ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.db_stats(
    IN p_database_name text,
    IN p_scale float8 DEFAULT 1,
    IN p_freeStorage bool DEFAULT false)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_db_stats$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.db_stats(text, float8, bool)
    IS 'Returns storage statistics for a given database';


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.db_stats_worker(
    IN p_collection_ids bigint[])
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
AS 'MODULE_PATHNAME', $function$command_db_stats_worker$function$;
