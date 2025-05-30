
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.aggregation_support(internal)
 RETURNS internal
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $$command_aggregation_support$$;
