
/* apply schema validation while data updating */
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.schema_validation_against_update(
    p_eval_state bytea,
    p_target_document __CORE_SCHEMA__.bson,
    p_source_document __CORE_SCHEMA__.bson,
    p_is_moderate boolean
   )
RETURNS boolean
LANGUAGE C
  STRICT
AS 'MODULE_PATHNAME', $$command_schema_validation_against_update$$;