/**
 * @brief Validates the indexes for a given collection.
 *
 * @param[in] database The name of the database containing the collection to be validated.
 * @param[in] validateSpec BSON object specifying the validation options.
 * @param[out] document OUT parameter that will contain the validation result.
 *
 * @return BSON object containing the validation result.
 *
 * @ingroup commands_diagnostic
 *
 * **Usage Examples:**
 * ```SQL 
 * SELECT documentdb_api.create_collection('db', 'validatecoll');
 * NOTICE:  creating collection
 *  create_collection 
 *  ---------------------------------------------------------------------
 *  t
 * (1 row)
 * ```
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.validate(database text, validateSpec __CORE_SCHEMA__.bson, OUT document __CORE_SCHEMA__.bson)
RETURNS __CORE_SCHEMA__.bson
LANGUAGE C
VOLATILE PARALLEL UNSAFE STRICT
AS 'MODULE_PATHNAME', $function$command_validate$function$;
COMMENT ON FUNCTION __API_SCHEMA_V2__.validate(text, __CORE_SCHEMA__.bson)
    IS 'Validates the indexes for a given collection';
