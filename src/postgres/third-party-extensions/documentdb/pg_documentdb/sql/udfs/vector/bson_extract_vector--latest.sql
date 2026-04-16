
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_extract_vector(document __CORE_SCHEMA__.bson, path text)
 RETURNS public.vector
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE RETURNS NULL ON NULL INPUT
AS 'MODULE_PATHNAME', $function$command_bson_extract_vector$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_search_param(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_search_param$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_document_add_score_field(__CORE_SCHEMA__.bson, float8)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_document_add_score_field$function$;