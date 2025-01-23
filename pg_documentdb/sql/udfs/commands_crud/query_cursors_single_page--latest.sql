/**
 * @ingroup commands_crud
 * @brief Executes a count query in the specified database.
 *
 * @details This function performs a count operation based on the provided BSON-formatted
 *          specification. It retrieves a document containing the count result.
 *
 * **Usage Examples:**
 * - Execute a count query:
 *   ```sql
 *   SELECT documentdb_api.count_query(
 *       'my_database',
 *       '{"count": "my_collection", "query": { "status": "active" }}');
 *   -- { "n" : { "$numberInt" : "3" }, "ok" : { "$numberDouble" : "1.0" } }
 *   ```
 *

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] countSpec BSON object containing the count query specification. Must include the "count" collection and a valid "query".
 * @param[out] document BSON object containing the result of the count query.
 *
 * **Returns:**
 * A BSON document with the following field:
 * - `document`: BSON object containing the count result, e.g., `{ "count": 42 }`.
 *   ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.count_query(database text, countSpec __CORE_SCHEMA__.bson, OUT document __CORE_SCHEMA__.bson)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_count_query$function$;

/**
 * @ingroup commands_crud
 * @brief Executes a distinct query in the specified database.
 *
 * @details This function retrieves distinct values from a collection based on the 
 *          provided BSON-formatted specification.
 *
 * **Usage Examples:**
 * - Execute a distinct query:
 *   ```sql
 *    SELECT document FROM documentdb_api.distinct_query('db', '{ "distinct": "aggregation_pipeline"}');
 *                               document                               
 *   ---------------------------------------------------------------------
 *   { "values" : [ "1", "2", "3" ], "ok" : { "$numberDouble" : "1.0" } }
 *   (1 row)
 *   ```
 * 

 * @param[in] database Name of the target database. Must not be NULL.
 * @param[in] distinctSpec BSON object containing the distinct query specification. Must include the "distinct" field specifying the collection and a valid "key".
 * @param[out] document BSON object containing the result of the distinct query.
 *
 * **Returns:**
 * A BSON document with the following field:
 * - `document`: BSON object containing the distinct query result, and the distinct values.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA__.distinct_query(database text, distinctSpec __CORE_SCHEMA__.bson, OUT document __CORE_SCHEMA__.bson)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_distinct_query$function$;