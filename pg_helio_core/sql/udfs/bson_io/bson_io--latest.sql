
CREATE OR REPLACE FUNCTION bson_get_value(bson, text)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_get_value$function$;

CREATE OR REPLACE FUNCTION bson_get_value_text(bson, text)
 RETURNS text
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_get_value_text$function$;

CREATE OR REPLACE FUNCTION bson_from_bytea(bytea)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_from_bytea$function$;

CREATE OR REPLACE FUNCTION bson_to_bytea(bson)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bytea$function$;

CREATE OR REPLACE FUNCTION bson_to_bson_hex(bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bson_hex$function$;

CREATE OR REPLACE FUNCTION bson_hex_to_bson(cstring)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_hex_to_bson$function$;

CREATE OR REPLACE FUNCTION bson_json_to_bson(text)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_json_to_bson$function$;

CREATE OR REPLACE FUNCTION bson_to_json_string(bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_json_string$function$;

CREATE OR REPLACE FUNCTION row_get_bson(record)
  RETURNS bson
  LANGUAGE c
  IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$row_get_bson$function$;

CREATE OR REPLACE FUNCTION bson_object_keys(bson)
 RETURNS SETOF text
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $$bson_object_keys$$;

CREATE OR REPLACE FUNCTION bson_repath_and_build(VARIADIC "any")
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_repath_and_build$function$;

DROP CAST IF EXISTS (bytea AS bson);
CREATE CAST (bytea AS bson)
 WITH FUNCTION bson_from_bytea(bytea) AS IMPLICIT;

DROP CAST IF EXISTS (bson AS bytea);
CREATE CAST (bson AS bytea)
 WITH FUNCTION bson_to_bytea(bson);
