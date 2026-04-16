
CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_from_bytea(bytea)
 RETURNS __CORE_SCHEMA__.bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_from_bytea$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_to_bytea(__CORE_SCHEMA__.bsonsequence)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_to_bytea$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_to_bsonsequence(__CORE_SCHEMA__.bson)
 RETURNS __CORE_SCHEMA__.bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bsonsequence$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_get_bson(__CORE_SCHEMA__.bsonsequence)
 RETURNS SETOF __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bsonsequence_get_bson$function$;

DROP CAST IF EXISTS (bytea AS __CORE_SCHEMA__.bsonsequence);
CREATE CAST (bytea AS __CORE_SCHEMA__.bsonsequence)
 WITH FUNCTION __CORE_SCHEMA__.bsonsequence_from_bytea(bytea) AS IMPLICIT;

DROP CAST IF EXISTS (__CORE_SCHEMA__.bsonsequence AS bytea);
CREATE CAST (__CORE_SCHEMA__.bsonsequence AS bytea)
 WITH FUNCTION __CORE_SCHEMA__.bsonsequence_to_bytea(__CORE_SCHEMA__.bsonsequence);

DROP CAST IF EXISTS (__CORE_SCHEMA__.bson AS __CORE_SCHEMA__.bsonsequence);
CREATE CAST (__CORE_SCHEMA__.bson AS __CORE_SCHEMA__.bsonsequence)
 WITH FUNCTION __CORE_SCHEMA__.bson_to_bsonsequence(__CORE_SCHEMA__.bson);
