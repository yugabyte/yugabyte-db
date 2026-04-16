-- ----------------------------------------------------------------
-- Misc Tests on Hash Function
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

-- ---------------- boolean

SELECT hll_hash_boolean(FALSE);
SELECT hll_hash_boolean(TRUE);

-- ---------------- smallint

SELECT hll_hash_smallint(0::smallint);
SELECT hll_hash_smallint(100::smallint);
SELECT hll_hash_smallint(-100::smallint);

-- ---------------- integer

SELECT hll_hash_integer(0);
SELECT hll_hash_integer(100);
SELECT hll_hash_integer(-100);

-- ---------------- bigint

SELECT hll_hash_bigint(0);
SELECT hll_hash_bigint(100);
SELECT hll_hash_bigint(-100);

-- ---------------- bytea

-- Check some small values.
SELECT hll_hash_bytea(E'\\x');
SELECT hll_hash_bytea(E'\\x41');
SELECT hll_hash_bytea(E'\\x42');
SELECT hll_hash_bytea(E'\\x4142');

-- ---------------- text

-- Check some small values.
SELECT hll_hash_text('');
SELECT hll_hash_text('A');
SELECT hll_hash_text('B');
SELECT hll_hash_text('AB');

-- ---------------- seed stuff

-- Seed 0 ok.
SELECT hll_hash_bigint(0, 0);

-- Positive seed ok.
SELECT hll_hash_bigint(0, 1);

-- Max positive seed ok.
SELECT hll_hash_bigint(0, 2147483647);

-- WARNING:  negative seed values not compatible
SELECT hll_hash_bigint(0, -1);

-- WARNING:  negative seed values not compatible
SELECT hll_hash_bigint(0, -2);

-- WARNING:  negative seed values not compatible
SELECT hll_hash_bigint(0, -2147483648);

-- WARNING:  negative seed values not compatible
SELECT hll_hash_boolean(0, -1);
SELECT hll_hash_smallint(0::smallint, -1);
SELECT hll_hash_integer(0, -1);
SELECT hll_hash_bigint(0, -1);
SELECT hll_hash_bytea(E'\\x', -1);
SELECT hll_hash_text('AB', -1);

-- ---------------- Matches

SELECT hll_hash_boolean(FALSE, 0), hll_hash_bytea(E'\\x00', 0);
SELECT hll_hash_boolean(TRUE, 0), hll_hash_bytea(E'\\x01', 0);
SELECT hll_hash_smallint(0::smallint, 0), hll_hash_bytea(E'\\x0000', 0);
SELECT hll_hash_integer(0, 0), hll_hash_bytea(E'\\x00000000', 0);
SELECT hll_hash_bigint(0, 0),  hll_hash_bytea(E'\\x0000000000000000', 0);
SELECT hll_hash_bytea(E'\\x4142', 0), hll_hash_text('AB', 0);

-- ---------------- Default seed = 0

SELECT hll_hash_boolean(TRUE) = hll_hash_boolean(TRUE, 0);
SELECT hll_hash_smallint(100::smallint) = hll_hash_smallint(100::smallint, 0);
SELECT hll_hash_integer(100) = hll_hash_integer(100, 0);
SELECT hll_hash_bigint(100) = hll_hash_bigint(100, 0);
SELECT hll_hash_bytea(E'\\x42') = hll_hash_bytea(E'\\x42', 0);
SELECT hll_hash_text('AB') = hll_hash_text('AB', 0);

-- ---------------- Explicit casts work for already hashed numbers.

SELECT hll_empty() || 42::hll_hashval;
SELECT hll_empty() || CAST(42 AS hll_hashval);
SELECT hll_empty() || 42::bigint::hll_hashval;
SELECT hll_empty() || CAST(42 AS bigint)::hll_hashval;

-- ERROR: doesn't cast implicitly
SELECT hll_empty() || 42;
SELECT hll_empty() || 42::bigint;

