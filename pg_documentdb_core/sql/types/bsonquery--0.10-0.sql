CREATE TYPE bsonquery;

CREATE OR REPLACE FUNCTION bsonquery_in(cstring)
 RETURNS bsonquery
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in$function$;

CREATE OR REPLACE FUNCTION bsonquery_out(bsonquery)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_out$function$;

CREATE OR REPLACE FUNCTION bsonquery_send(bsonquery)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_send$function$;

CREATE OR REPLACE FUNCTION bsonquery_recv(internal)
 RETURNS bsonquery
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_recv$function$;

CREATE TYPE bsonquery (
    input = bsonquery_in,
    output = bsonquery_out,
    send = bsonquery_send,
    receive = bsonquery_recv,
    alignment = int4,
    storage = extended
);