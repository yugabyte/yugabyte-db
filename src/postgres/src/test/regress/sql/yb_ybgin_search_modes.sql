--
-- Yugabyte-owned test for ybgin index access method and search modes.
--

-- Disable sequential scan so that index scan is always chosen.
SET enable_seqscan = off;

CREATE INDEX NONCONCURRENTLY idx_partial ON arrays
    USING ybgin (a)
    WHERE a <@ '{1}' or a @> '{}' or a is null;

-- GIN_SEARCH_MODE_INCLUDE_EMPTY
SELECT * FROM arrays WHERE a <@ '{1}';
-- GIN_SEARCH_MODE_ALL
SELECT * FROM arrays WHERE a @> '{}';
-- GIN_CAT_NULL_ITEM
SELECT * FROM arrays WHERE a is null;

-- Cleanup
DROP INDEX idx_partial;
