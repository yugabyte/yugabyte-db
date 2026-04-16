-- ----------------------------------------------------------------
-- Misc Tests on hash_any Function
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

-- ---------------- Check hash and hash_any function results match

SELECT hll_hash_boolean(FALSE) = hll_hash_any(FALSE);
SELECT hll_hash_boolean(TRUE) = hll_hash_any(TRUE);

SELECT hll_hash_smallint(0::smallint) = hll_hash_any(0::smallint);
SELECT hll_hash_smallint(100::smallint) = hll_hash_any(100::smallint);
SELECT hll_hash_smallint(-100::smallint) = hll_hash_any(-100::smallint);

SELECT hll_hash_integer(0) = hll_hash_any(0);
SELECT hll_hash_integer(100) = hll_hash_any(100);
SELECT hll_hash_integer(-100) = hll_hash_any(-100);

SELECT hll_hash_bigint(0) = hll_hash_any(0::bigint);
SELECT hll_hash_bigint(100) = hll_hash_any(100::bigint);
SELECT hll_hash_bigint(-100) = hll_hash_any(-100::bigint);

SELECT hll_hash_bytea(E'\\x') = hll_hash_any(E'\\x'::bytea);
SELECT hll_hash_bytea(E'\\x41') = hll_hash_any(E'\\x41'::bytea);
SELECT hll_hash_bytea(E'\\x42') = hll_hash_any(E'\\x42'::bytea);
SELECT hll_hash_bytea(E'\\x4142') = hll_hash_any(E'\\x4142'::bytea);

SELECT hll_hash_text('') = hll_hash_any(''::text);
SELECT hll_hash_text('A') = hll_hash_any('A'::text);
SELECT hll_hash_text('B') = hll_hash_any('B'::text);
SELECT hll_hash_text('AB') = hll_hash_any('AB'::text);

-- ---------------- Check several types not handled by default hash functions

-- ---------------- macaddr

SELECT hll_hash_any('08:00:2b:01:02:03'::macaddr);
SELECT hll_hash_any('08002b010203'::macaddr);
SELECT hll_hash_any('01-23-45-67-89-ab'::macaddr);
SELECT hll_hash_any('012345-6789ab'::macaddr);

-- ---------------- interval

SELECT hll_hash_any('1 year 2 months 3 days 4 hours 5 minutes 6seconds'::interval);
SELECT hll_hash_any('P1Y2M3DT4H5M6S'::interval);
SELECT hll_hash_any('1997-06 20 12:00:00'::interval);
SELECT hll_hash_any('P1997-06-20T12:00:00'::interval);
