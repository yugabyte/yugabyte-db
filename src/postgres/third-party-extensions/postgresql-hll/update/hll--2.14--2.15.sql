#if PG_VERSION_NUM < 100000
#define IFPARALLEL(...)
#else
#define IFPARALLEL(...) __VA_ARGS__
#endif

CREATE FUNCTION hll_serialize(internal)
RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE IFPARALLEL(PARALLEL SAFE);

CREATE FUNCTION hll_deserialize(bytea, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE IFPARALLEL(PARALLEL SAFE);

CREATE FUNCTION hll_union_internal(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IFPARALLEL(PARALLEL SAFE);

IFPARALLEL(
ALTER FUNCTION hll_in(cstring, oid, integer) PARALLEL SAFE;
ALTER FUNCTION hll_out(hll) PARALLEL SAFE;
ALTER FUNCTION hll_recv(internal) PARALLEL SAFE;
ALTER FUNCTION hll_send(hll) PARALLEL SAFE;
ALTER FUNCTION hll_typmod_in(cstring[]) PARALLEL SAFE;
ALTER FUNCTION hll_typmod_out(integer) PARALLEL SAFE;
ALTER FUNCTION hll(hll, integer, boolean) PARALLEL SAFE;
ALTER FUNCTION hll_hashval_in(cstring, oid, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hashval_out(hll_hashval) PARALLEL SAFE;
ALTER FUNCTION hll_hashval_eq(hll_hashval, hll_hashval) PARALLEL SAFE;
ALTER FUNCTION hll_hashval_ne(hll_hashval, hll_hashval) PARALLEL SAFE;
ALTER FUNCTION hll_hashval(bigint) PARALLEL SAFE;
ALTER FUNCTION hll_hashval_int4(integer) PARALLEL SAFE;
ALTER FUNCTION hll_eq(hll, hll) PARALLEL SAFE;
ALTER FUNCTION hll_ne(hll, hll) PARALLEL SAFE;
ALTER FUNCTION hll_cardinality(hll) PARALLEL SAFE;
ALTER FUNCTION hll_union(hll, hll) PARALLEL SAFE;
ALTER FUNCTION hll_add(hll, hll_hashval) PARALLEL SAFE;
ALTER FUNCTION hll_add_rev(hll_hashval, hll) PARALLEL SAFE;
ALTER FUNCTION hll_print(hll) PARALLEL SAFE;
ALTER FUNCTION hll_empty() PARALLEL SAFE;
ALTER FUNCTION hll_empty(integer) PARALLEL SAFE;
ALTER FUNCTION hll_empty(integer, integer) PARALLEL SAFE;
ALTER FUNCTION hll_empty(integer, integer, bigint) PARALLEL SAFE;
ALTER FUNCTION hll_empty(integer, integer, bigint, integer) PARALLEL SAFE;
ALTER FUNCTION hll_schema_version(hll) PARALLEL SAFE;
ALTER FUNCTION hll_type(hll) PARALLEL SAFE;
ALTER FUNCTION hll_log2m(hll) PARALLEL SAFE;
ALTER FUNCTION hll_regwidth(hll) PARALLEL SAFE;
ALTER FUNCTION hll_expthresh(hll, OUT specified bigint, OUT effective bigint) PARALLEL SAFE;
ALTER FUNCTION hll_sparseon(hll) PARALLEL SAFE;
ALTER FUNCTION hll_hash_boolean(boolean, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_smallint(smallint, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_integer(integer, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_bigint(bigint, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_bytea(bytea, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_text(text, integer) PARALLEL SAFE;
ALTER FUNCTION hll_hash_any(anyelement, integer) PARALLEL SAFE;
ALTER FUNCTION hll_union_trans(internal, hll) PARALLEL SAFE;
ALTER FUNCTION hll_add_trans4 PARALLEL SAFE;
ALTER FUNCTION hll_add_trans3 PARALLEL SAFE;
ALTER FUNCTION hll_add_trans2 PARALLEL SAFE;
ALTER FUNCTION hll_add_trans1 PARALLEL SAFE;
ALTER FUNCTION hll_add_trans0 PARALLEL SAFE;
ALTER FUNCTION hll_pack(internal) PARALLEL SAFE;
ALTER FUNCTION hll_card_unpacked(internal) PARALLEL SAFE;
ALTER FUNCTION hll_floor_card_unpacked(internal) PARALLEL SAFE;
ALTER FUNCTION hll_ceil_card_unpacked(internal) PARALLEL SAFE;

DROP AGGREGATE hll_union_agg (hll);
DROP AGGREGATE hll_add_agg (hll_hashval);
DROP AGGREGATE hll_add_agg (hll_hashval, integer);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint, integer);

CREATE AGGREGATE hll_union_agg (hll) (
       SFUNC = hll_union_trans,
       STYPE = internal,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);

CREATE AGGREGATE hll_add_agg (hll_hashval) (
       SFUNC = hll_add_trans0,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer) (
       SFUNC = hll_add_trans1,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer) (
       SFUNC = hll_add_trans2,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint) (
       SFUNC = hll_add_trans3,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint, integer) (
       SFUNC = hll_add_trans4,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack,
       COMBINEFUNC = hll_union_internal,
       SERIALFUNC = hll_serialize,
       DESERIALFUNC = hll_deserialize,
       PARALLEL = SAFE
);
)
