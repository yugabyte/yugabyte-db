
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_exists(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_exists$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_type(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_type$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_bits_all_clear(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_bits_all_clear$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_bits_any_clear(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_bits_any_clear$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_bits_all_set(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_bits_all_set$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_bits_any_set(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_bits_any_set$function$;
