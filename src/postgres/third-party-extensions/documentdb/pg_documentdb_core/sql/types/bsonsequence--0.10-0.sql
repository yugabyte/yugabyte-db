
CREATE TYPE __CORE_SCHEMA__.bsonsequence;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_in(cstring)
 RETURNS __CORE_SCHEMA__.bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_in$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_out(__CORE_SCHEMA__.bsonsequence)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_out$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_send(__CORE_SCHEMA__.bsonsequence)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_send$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bsonsequence_recv(internal)
 RETURNS __CORE_SCHEMA__.bsonsequence
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bsonsequence_recv$function$;

CREATE TYPE __CORE_SCHEMA__.bsonsequence (
    input = __CORE_SCHEMA__.bsonsequence_in,
    output = __CORE_SCHEMA__.bsonsequence_out,
    send = __CORE_SCHEMA__.bsonsequence_send,
    receive = __CORE_SCHEMA__.bsonsequence_recv,
    alignment = int4,
    storage = extended
);
