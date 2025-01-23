/**
 * @ingroup utils
 * @brief Retrieves the binary version of the DocumentDB API.
 *
 * @details This function returns the version string of the DocumentDB API binary.
 *
 * @returns A text string representing the binary version.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.binary_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($function$get_, __API_SCHEMA_V2__, _binary_version$function$);

/**
 * @ingroup utils
 * @brief Retrieves the extended binary version of the DocumentDB API.
 *
 * @details This function returns a more detailed version string of the DocumentDB API binary,
 *          including additional build metadata.
 *
 * @returns A text string representing the extended binary version.
 */
CREATE OR REPLACE FUNCTION __API_SCHEMA_V2__.binary_extended_version()
RETURNS text
LANGUAGE C
IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', __CONCAT_NAME_FUNCTION__($function$get_, __API_SCHEMA_V2__, _extended_binary_version$function$);