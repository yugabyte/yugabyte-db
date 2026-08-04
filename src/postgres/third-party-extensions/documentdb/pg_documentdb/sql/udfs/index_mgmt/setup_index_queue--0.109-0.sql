CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.setup_index_queue_table(major_version integer, minor_version integer, patch_version integer)
  RETURNS void
  LANGUAGE C
AS 'MODULE_PATHNAME', $$setup_index_queue_table$$;