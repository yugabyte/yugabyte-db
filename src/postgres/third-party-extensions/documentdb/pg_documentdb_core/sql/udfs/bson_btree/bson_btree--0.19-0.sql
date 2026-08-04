

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_compare$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_equal(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_equal$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_not_equal(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_not_equal$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_lt(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_lt$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_lte(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_lte$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_gt(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_gt$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_gte(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_gte$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_unique_index_equal(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_unique_index_equal$function$;

-- in_range support functions
CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_in_range_numeric(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, bool, bool)
 RETURNS bool
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in_range_numeric$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_in_range_interval(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, interval, bool, bool)
 RETURNS bool
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in_range_interval$function$;
