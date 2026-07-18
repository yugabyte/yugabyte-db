DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL_V2__.apply_extension_data_table_upgrade;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.apply_extension_data_table_upgrade(int4, int4, int4)
RETURNS void
LANGUAGE c
STRICT
AS 'MODULE_PATHNAME', $function$apply_extension_data_table_upgrade$function$;
COMMENT ON FUNCTION __API_SCHEMA_INTERNAL_V2__.apply_extension_data_table_upgrade(int4, int4, int4)
    IS 'alter creation time column';