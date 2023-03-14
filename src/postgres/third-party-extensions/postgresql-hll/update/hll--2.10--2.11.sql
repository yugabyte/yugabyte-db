DROP AGGREGATE hll_add_agg (hll_hashval);
DROP AGGREGATE hll_add_agg (hll_hashval, integer);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint);
DROP AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint, integer);

CREATE AGGREGATE hll_add_agg (hll_hashval) (
       SFUNC = hll_add_trans0,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer) (
       SFUNC = hll_add_trans1,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer) (
       SFUNC = hll_add_trans2,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint) (
       SFUNC = hll_add_trans3,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack
);

CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint, integer) (
       SFUNC = hll_add_trans4,
       STYPE = internal,
       SSPACE = 131120,
       FINALFUNC = hll_pack
);
