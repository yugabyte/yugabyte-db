CREATE TYPE __CORE_SCHEMA__.bsonquery;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_in(cstring)
 RETURNS __CORE_SCHEMA__.bsonquery
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_out(__CORE_SCHEMA__.bsonquery)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_out$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_send(__CORE_SCHEMA__.bsonquery)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_send$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonquery_recv(internal)
 RETURNS __CORE_SCHEMA__.bsonquery
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_recv$function$;

CREATE TYPE __CORE_SCHEMA__.bsonquery (
    input = __CORE_SCHEMA__.bsonquery_in,
    output = __CORE_SCHEMA__.bsonquery_out,
    send = __CORE_SCHEMA__.bsonquery_send,
    receive = __CORE_SCHEMA__.bsonquery_recv,
    alignment = int4,
    storage = extended
);