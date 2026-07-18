
CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_get_value(__CORE_SCHEMA__.bson, text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_get_value$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_get_value_text(__CORE_SCHEMA__.bson, text)
 RETURNS text
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_get_value_text$function$;


CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_get_value_text(__CORE_SCHEMA__.bson, text, bool)
 RETURNS text
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_get_value_text$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_from_bytea(bytea)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_from_bytea$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_to_bytea(__CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bytea$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_to_bson_hex(__CORE_SCHEMA__.bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bson_hex$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_hex_to_bson(cstring)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_hex_to_bson$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_json_to_bson(text)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_json_to_bson$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_to_json_string(__CORE_SCHEMA__.bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_json_string$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.row_get_bson(record)
  RETURNS __CORE_SCHEMA__.bson
  LANGUAGE c
  IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$row_get_bson$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_object_keys(__CORE_SCHEMA__.bson)
 RETURNS SETOF text
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $$bson_object_keys$$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_repath_and_build(VARIADIC "any")
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_repath_and_build$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_build_document(VARIADIC "any")
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_build_document$function$;

DROP CAST IF EXISTS (bytea AS __CORE_SCHEMA__.bson);
CREATE CAST (bytea AS __CORE_SCHEMA__.bson)
 WITH FUNCTION __CORE_SCHEMA__.bson_from_bytea(bytea) AS IMPLICIT;

DROP CAST IF EXISTS (__CORE_SCHEMA__.bson AS bytea);
CREATE CAST (__CORE_SCHEMA__.bson AS bytea)
WITH FUNCTION __CORE_SCHEMA__.bson_to_bytea(__CORE_SCHEMA__.bson);
