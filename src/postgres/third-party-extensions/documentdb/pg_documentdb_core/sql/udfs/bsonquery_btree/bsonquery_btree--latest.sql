
CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_compare(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_compare$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_compare(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bsonquery)
 RETURNS int
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_compare$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_equal(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_equal$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_not_equal(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_not_equal$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_lt(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_lt$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_lte(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_lte$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_gt(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_gt$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_gte(__CORE_SCHEMA__.bsonquery, __CORE_SCHEMA__.bsonquery)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonquery_gte$function$;
