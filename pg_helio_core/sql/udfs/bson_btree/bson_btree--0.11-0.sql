

CREATE OR REPLACE FUNCTION bson_compare(bson, bson)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_compare$function$;

CREATE OR REPLACE FUNCTION bson_equal(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_equal$function$;

CREATE OR REPLACE FUNCTION bson_not_equal(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_not_equal$function$;

CREATE OR REPLACE FUNCTION bson_lt(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_lt$function$;

CREATE OR REPLACE FUNCTION bson_lte(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_lte$function$;

CREATE OR REPLACE FUNCTION bson_gt(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_gt$function$;

CREATE OR REPLACE FUNCTION bson_gte(bson, bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$extension_bson_gte$function$;

CREATE OR REPLACE FUNCTION bson_unique_index_equal(bson, bson)
 RETURNS bool
 LANGUAGE C
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_unique_index_equal$function$;
