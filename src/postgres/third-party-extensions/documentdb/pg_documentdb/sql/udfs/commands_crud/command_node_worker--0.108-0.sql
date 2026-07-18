

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.command_node_worker(
    p_local_function_oid Oid,
    p_local_function_arg __CORE_SCHEMA_V2__.bson,
    p_current_table regclass,
    p_chosen_tables text[],
    p_tables_qualified boolean,
    p_optional_arg_unused text)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE C
AS 'MODULE_PATHNAME', $$command_node_worker$$;