CREATE OR REPLACE FUNCTION __API_CATALOG_SCHEMA__.bson_orderby(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_orderby$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby(document __CORE_SCHEMA__.bson, filter __CORE_SCHEMA__.bson, collationString text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$command_bson_orderby$function$;


-- ORDER BY operator function for $setWindowFields
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_partition(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson, boolean)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_partition$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_partition(document __CORE_SCHEMA__.bson, filter __CORE_SCHEMA__.bson, isTimeRangeWindow boolean, collationString text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_partition$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_compare$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_lt(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_lt$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_eq(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_eq$function$;


CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bson_orderby_gt(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_orderby_gt$function$;
