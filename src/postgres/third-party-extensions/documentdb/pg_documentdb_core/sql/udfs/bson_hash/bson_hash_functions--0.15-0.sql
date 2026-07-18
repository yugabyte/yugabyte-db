
CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_hash_int4(__CORE_SCHEMA__.bson)
 RETURNS int4
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_hash_int4$function$;


CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_hash_int8(__CORE_SCHEMA__.bson, int8)
 RETURNS int8
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_hash_int8$function$;
