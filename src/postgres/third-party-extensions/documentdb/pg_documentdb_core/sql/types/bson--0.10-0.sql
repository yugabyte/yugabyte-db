

-- YB: Set the hard coded oid.
SET yb_binary_restore TO true;
SELECT binary_upgrade_set_next_pg_type_oid(8095);
CREATE TYPE __CORE_SCHEMA__.bson;
SET yb_binary_restore TO false;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_in(cstring)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_in$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_out(__CORE_SCHEMA__.bson)
 RETURNS cstring
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_out$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_send(__CORE_SCHEMA__.bson)
 RETURNS bytea
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_send$function$;

CREATE OR REPLACE FUNCTION __CORE_SCHEMA__.bson_recv(internal)
 RETURNS __CORE_SCHEMA__.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'MODULE_PATHNAME', $function$bson_recv$function$;

CREATE TYPE __CORE_SCHEMA__.bson (
    input = __CORE_SCHEMA__.bson_in,
    output = __CORE_SCHEMA__.bson_out,
    send = __CORE_SCHEMA__.bson_send,
    receive = __CORE_SCHEMA__.bson_recv,
    alignment = int4,
    storage = extended
);