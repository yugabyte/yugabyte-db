CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.index_build_is_in_progress(
    p_index_id int)
 RETURNS boolean
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_index_build_is_in_progress$$;
