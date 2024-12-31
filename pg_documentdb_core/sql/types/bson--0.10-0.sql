

CREATE TYPE bson;

CREATE OR REPLACE FUNCTION bson_in(cstring)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in$function$;

CREATE OR REPLACE FUNCTION bson_out(bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_out$function$;

CREATE OR REPLACE FUNCTION bson_send(bson)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_send$function$;

CREATE OR REPLACE FUNCTION bson_recv(internal)
 RETURNS bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_recv$function$;

CREATE TYPE bson (
    input = bson_in,
    output = bson_out,
    send = bson_send,
    receive = bson_recv,
    alignment = int4,
    storage = extended
);