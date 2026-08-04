-- ----------------------------------------------------------------
-- Regression tests for explicit threshold enforcement.
-- Verify that auto-threshold capping, buffer capacity guards,
-- and deserialization validation work correctly.
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

-- ----------------------------------------------------------------
-- Test 1: Crafted binary with out-of-range log2m is rejected.
-- byte0=0x11 (v1, EMPTY), byte1=0xf4 (regwidth=8, log2m=20),
-- byte2=0x3f (sparseon=0, expthresh=auto).
-- Must be rejected at deserialization time.
-- ----------------------------------------------------------------

SELECT '\x11f43f'::hll;

-- Explicit type with out-of-range params also rejected.
-- byte0=0x12 (v1, EXPLICIT), same params, one 8-byte element.
SELECT '\x12f43f0000000000000001'::hll;

-- Malformed header variant: byte2=0x7f (sparseon=1, expthresh=auto).
SELECT '\x12f47f0000000000000001'::hll;

-- ----------------------------------------------------------------
-- Test 2: Crafted binary with out-of-range regwidth is rejected.
-- byte1=0xeb (regwidth=8, log2m=11).
-- ----------------------------------------------------------------

SELECT '\x11eb3f'::hll;

-- ----------------------------------------------------------------
-- Test 3: Valid parameters, verify no behavioral change.
-- Standard log2m=11, regwidth=5 with auto threshold.
-- ----------------------------------------------------------------

SELECT hll_cardinality(
    hll_add(
    hll_add(
    hll_add(hll_empty(11,5,-1,1),
        hll_hash_integer(1,0)),
        hll_hash_integer(2,0)),
        hll_hash_integer(3,0))
);

-- ----------------------------------------------------------------
-- Test 4: Union with valid small parameters.
-- ----------------------------------------------------------------

SELECT hll_cardinality(
    hll_union(
        hll_add(hll_empty(11,5,-1,1),
            hll_hash_integer(1,0)),
        hll_add(hll_empty(11,5,-1,1),
            hll_hash_integer(2,0))
    )
);

-- ----------------------------------------------------------------
-- Test 5: Promotion at explicit threshold boundary.
-- Use expthresh=2: two elements stay explicit, third promotes.
-- ----------------------------------------------------------------

-- Two elements: should be explicit (type 2).
SELECT hll_type(
    hll_add(
    hll_add(hll_empty(11,5,2,0),
        hll_hash_integer(1,0)),
        hll_hash_integer(2,0))
);

-- Three elements: should promote to compressed (type 4).
SELECT hll_type(
    hll_add(
    hll_add(
    hll_add(hll_empty(11,5,2,0),
        hll_hash_integer(1,0)),
        hll_hash_integer(2,0)),
        hll_hash_integer(3,0))
);

-- ----------------------------------------------------------------
-- Test 6: Promotion via union at threshold boundary.
-- Two explicit HLLs with expthresh=2, union forces promotion.
-- ----------------------------------------------------------------

SELECT hll_type(
    hll_union(
        hll_add(
        hll_add(hll_empty(11,5,2,0),
            hll_hash_integer(1,0)),
            hll_hash_integer(2,0)),
        hll_add(hll_empty(11,5,2,0),
            hll_hash_integer(3,0))
    )
);

-- ----------------------------------------------------------------
-- Test 7: Valid max-boundary parameters (log2m=17, regwidth=7).
-- These are at the limit but within valid range.
-- ----------------------------------------------------------------

SELECT hll_cardinality(
    hll_add(
    hll_add(
    hll_add(hll_empty(17,7,-1,0),
        hll_hash_integer(1,0)),
        hll_hash_integer(2,0)),
        hll_hash_integer(3,0))
);

-- Union with max-boundary parameters.
SELECT hll_cardinality(
    hll_union(
        hll_add(hll_empty(17,7,-1,0),
            hll_hash_integer(1,0)),
        hll_add(hll_empty(17,7,-1,0),
            hll_hash_integer(2,0))
    )
);

-- ----------------------------------------------------------------
-- Test 8: Crafted binary with valid max-boundary parameters.
-- byte1=0xcb (regwidth=7, log2m=11), byte2=0x3f (auto, sparse=0).
-- Should be accepted.
-- ----------------------------------------------------------------

SELECT hll_cardinality(
    hll_add('\x11cb3f'::hll, hll_hash_integer(1,0))
);

-- ----------------------------------------------------------------
-- Test 9: Malformed header rejected through hll_union() path.
-- The exact PoC header \x12f47f (log2m=20, regwidth=8, auto)
-- must be rejected even when used as input to hll_union().
-- ----------------------------------------------------------------

SELECT hll_union(
    '\x12f47f0000000000000001'::hll,
    '\x12f47f0000000000000002'::hll
);

-- ----------------------------------------------------------------
-- Test 10: Malformed header rejected through hll_union_agg() path.
-- ----------------------------------------------------------------

SELECT hll_union_agg(v::hll) FROM (VALUES
    ('\x12f47f0000000000000001'::hll),
    ('\x12f47f0000000000000002'::hll)
) AS t(v);

-- ----------------------------------------------------------------
-- Test 11: Near-capacity union with max valid parameters.
-- Build two large explicit HLLs with log2m=11, regwidth=5,
-- auto threshold. Auto threshold = ((5*2048+7)/8)/8 = 160.
-- Each HLL has 100 unique elements, union produces 200 which
-- exceeds the threshold and must promote to compressed.
-- ----------------------------------------------------------------

SELECT hll_type(
    hll_union(
        (SELECT hll_add_agg(hll_hash_integer(i,0), 11, 5, -1, 0)
         FROM generate_series(1, 100) AS g(i)),
        (SELECT hll_add_agg(hll_hash_integer(i,0), 11, 5, -1, 0)
         FROM generate_series(101, 200) AS g(i))
    )
);

-- ----------------------------------------------------------------
-- Test 12: Near-capacity union via hll_union_agg aggregate.
-- Build 20 explicit HLLs of 10 elements each with auto threshold.
-- Union should promote to compressed.
-- ----------------------------------------------------------------

SELECT hll_type(
    (SELECT hll_union_agg(h) FROM (
        SELECT hll_add_agg(hll_hash_integer(i,0), 11, 5, -1, 0) AS h
        FROM generate_series(1, 200) AS g(i)
        GROUP BY i / 10
    ) sub)
);

-- ----------------------------------------------------------------
-- Test 13: hll_send on union result with valid parameters.
-- Ensure the serialized output is well-formed (no leaked memory).
-- ----------------------------------------------------------------

SELECT length(hll_send(
    hll_union(
        (SELECT hll_add_agg(hll_hash_integer(i,0), 11, 5, -1, 0)
         FROM generate_series(1, 100) AS g(i)),
        (SELECT hll_add_agg(hll_hash_integer(i,0), 11, 5, -1, 0)
         FROM generate_series(101, 200) AS g(i))
    )
)) > 0 AS send_ok;

-- ----------------------------------------------------------------
-- Test 14: Maximum valid params (log2m=17, regwidth=7) with
-- enough elements to exceed auto threshold (14336) via union.
-- Must promote to compressed without overflow.
-- ----------------------------------------------------------------

SELECT hll_type(
    hll_union(
        (SELECT hll_add_agg(hll_hash_integer(i,0), 17, 7, -1, 0)
         FROM generate_series(1, 10000) AS g(i)),
        (SELECT hll_add_agg(hll_hash_integer(i,0), 17, 7, -1, 0)
         FROM generate_series(5001, 15000) AS g(i))
    )
);

-- ----------------------------------------------------------------
-- Test 15: Parallel aggregate serialize/deserialize round-trip.
-- Force parallel execution of hll_union_agg to exercise the
-- hll_serialize → hll_deserialize path. The result must match
-- the non-parallel result.
-- ----------------------------------------------------------------

CREATE TEMPORARY TABLE hll_parallel_test AS
    SELECT i, hll_add(hll_empty(11,5,-1,0), hll_hash_integer(i,0)) AS h
    FROM generate_series(1, 1000) AS g(i);
ANALYZE hll_parallel_test;

-- Non-parallel baseline.
SET max_parallel_workers_per_gather = 0;
SELECT hll_cardinality(hll_union_agg(h)) AS card_serial FROM hll_parallel_test;

-- Force parallel execution to exercise hll_serialize/hll_deserialize.
SET max_parallel_workers_per_gather = 2;
SET parallel_tuple_cost = 0;
SET parallel_setup_cost = 0;
SET min_parallel_table_scan_size = 0;
SELECT hll_cardinality(hll_union_agg(h)) AS card_parallel FROM hll_parallel_test;

-- Reset.
RESET max_parallel_workers_per_gather;
RESET parallel_tuple_cost;
RESET parallel_setup_cost;
RESET min_parallel_table_scan_size;
DROP TABLE hll_parallel_test;
