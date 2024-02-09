
CREATE OR REPLACE FUNCTION bsonsequence_from_bytea(bytea)
 RETURNS bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_from_bytea$function$;

CREATE OR REPLACE FUNCTION bsonsequence_to_bytea(bsonsequence)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_to_bytea$function$;

CREATE OR REPLACE FUNCTION bson_to_bsonsequence(bson)
 RETURNS bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_to_bsonsequence$function$;

CREATE OR REPLACE FUNCTION bsonsequence_get_bson(bsonsequence)
 RETURNS SETOF bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'MODULE_PATHNAME', $function$bsonsequence_get_bson$function$;

DROP CAST IF EXISTS (bytea AS bsonsequence);
CREATE CAST (bytea AS bsonsequence)
 WITH FUNCTION bsonsequence_from_bytea(bytea) AS IMPLICIT;

DROP CAST IF EXISTS (bsonsequence AS bytea);
CREATE CAST (bsonsequence AS bytea)
 WITH FUNCTION bsonsequence_to_bytea(bsonsequence);

DROP CAST IF EXISTS (bson AS bsonsequence);
CREATE CAST (bson AS bsonsequence)
 WITH FUNCTION bson_to_bsonsequence(bson);
