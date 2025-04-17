
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_project(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_project$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_project(document __CORE_SCHEMA_V2__.bson, pathSpec __CORE_SCHEMA_V2__.bson, variableSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_project$function$;


CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_project_find(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson, querySpec __CORE_SCHEMA__.bson DEFAULT NULL)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE CALLED ON NULL INPUT
AS 'MODULE_PATHNAME', $function$bson_dollar_project_find$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_project_find(document __CORE_SCHEMA_V2__.bson, pathSpec __CORE_SCHEMA_V2__.bson, querySpec __CORE_SCHEMA_V2__.bson, letVariableSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE CALLED ON NULL INPUT
AS 'MODULE_PATHNAME', $function$bson_dollar_project_find$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_add_fields(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_add_fields$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_add_fields(document __CORE_SCHEMA_V2__.bson, pathSpec __CORE_SCHEMA_V2__.bson, letVariableSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_add_fields$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_set(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_set$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_unset(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_unset$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_replace_root(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_replace_root$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_replace_root(document __CORE_SCHEMA_V2__.bson, pathSpec __CORE_SCHEMA_V2__.bson, variableSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_replace_root$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_facet_project(__CORE_SCHEMA__.bson, bool)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_facet_project$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_documents(document __CORE_SCHEMA__.bson, pathSpec __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_documents$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_merge_documents_at_path(leftDocument __CORE_SCHEMA__.bson, rightDocument __CORE_SCHEMA__.bson, fieldPath text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_merge_documents_at_path$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_dollar_lookup_expression_eval_merge(document __CORE_SCHEMA_V2__.bson, pathSpec __CORE_SCHEMA_V2__.bson, variableSpec __CORE_SCHEMA_V2__.bson)
 RETURNS __CORE_SCHEMA_V2__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_dollar_lookup_expression_eval_merge$function$;
