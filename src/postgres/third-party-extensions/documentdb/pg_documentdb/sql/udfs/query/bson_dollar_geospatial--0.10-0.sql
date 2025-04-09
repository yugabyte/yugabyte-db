
CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_geowithin(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_geowithin$function$;

CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_dollar_geointersects(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
 SUPPORT __API_CATALOG_SCHEMA__.dollar_support
AS 'MODULE_PATHNAME', $function$bson_dollar_geointersects$function$;
