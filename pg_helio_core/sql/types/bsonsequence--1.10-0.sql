
CREATE TYPE bsonsequence;

CREATE OR REPLACE FUNCTION bsonsequence_in(cstring)
 RETURNS bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_in$function$;

CREATE OR REPLACE FUNCTION bsonsequence_out(bsonsequence)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_out$function$;

CREATE OR REPLACE FUNCTION bsonsequence_send(bsonsequence)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_send$function$;

CREATE OR REPLACE FUNCTION bsonsequence_recv(internal)
 RETURNS bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_recv$function$;

CREATE TYPE bsonsequence (
    input = bsonsequence_in,
    output = bsonsequence_out,
    send = bsonsequence_send,
    receive = bsonsequence_recv,
    alignment = int4,
    storage = extended
);
