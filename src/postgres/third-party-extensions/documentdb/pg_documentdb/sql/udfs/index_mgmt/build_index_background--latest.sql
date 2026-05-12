/* 
 * Drop-in replacement for build_index_concurrently. This will be called periodically by the 
 * background worker framework and will coexist with the previous UDF until it reaches
 * stability.
 */
CREATE OR REPLACE PROCEDURE __API_SCHEMA_INTERNAL__.build_index_background(IN p_job_index int)
 LANGUAGE C
AS 'MODULE_PATHNAME', $procedure$command_build_index_background$procedure$;
COMMENT ON PROCEDURE __API_SCHEMA_INTERNAL__.build_index_background(int)
    IS 'Builds a index for a collection';