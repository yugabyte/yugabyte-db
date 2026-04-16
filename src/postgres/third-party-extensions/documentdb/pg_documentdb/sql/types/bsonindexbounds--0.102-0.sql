CREATE TYPE __API_SCHEMA_INTERNAL_V2__.bsonindexbounds;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_in(cstring)
 RETURNS __API_SCHEMA_INTERNAL_V2__.bsonindexbounds
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_out(__API_SCHEMA_INTERNAL_V2__.bsonindexbounds)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_out$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_send(__API_SCHEMA_INTERNAL_V2__.bsonindexbounds)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_send$function$;

CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_recv(internal)
 RETURNS __API_SCHEMA_INTERNAL_V2__.bsonindexbounds
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_recv$function$;

CREATE TYPE __API_SCHEMA_INTERNAL_V2__.bsonindexbounds (
    input = __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_in,
    output = __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_out,
    send = __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_send,
    receive = __API_SCHEMA_INTERNAL_V2__.bsonindexbounds_recv,
    alignment = int4,
    storage = extended
);