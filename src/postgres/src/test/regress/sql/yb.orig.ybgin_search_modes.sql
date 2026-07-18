--
-- Yugabyte-owned test for ybgin index access method and search modes.
--

-- Always choose index scan.
SET enable_seqscan = off;
SET yb_test_ybgin_disable_cost_factor = 0.5;

CREATE INDEX NONCONCURRENTLY idx_partial ON arrays
    USING ybgin (a)
    WHERE a <@ '{1}' or a @> '{}' or a is null;

-- GIN_SEARCH_MODE_INCLUDE_EMPTY
SELECT * FROM arrays WHERE a <@ '{1}';
-- GIN_SEARCH_MODE_ALL
SELECT * FROM arrays WHERE a @> '{}';
-- GIN_SEARCH_MODE_EVERYTHING, GIN_CAT_NULL_ITEM
SELECT * FROM arrays WHERE a is null;

-- Cleanup
DROP INDEX idx_partial;
