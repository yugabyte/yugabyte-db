--
-- See yb_merge_scan_schedule for details about the test.  Tests that
-- dropped table columns do not mess up the merge and fetch,
--

SET yb_max_merge_scan_streams to 64;
SET enable_sort to off;

CREATE TABLE drop_col_range (
    x1 int,
    k1 int,
    x2 int,
    k2 int,
    x3 int,
    i1 int,
    x4 int,
    i2 int,
    x5 int,
    v text,
    x6 int,
    PRIMARY KEY (k1 ASC, k2 ASC));
CREATE INDEX ON drop_col_range (i1 ASC, i2 ASC);
ALTER TABLE drop_col_range DROP COLUMN x1, DROP COLUMN x2, DROP COLUMN x3, DROP COLUMN x4, DROP COLUMN x5, DROP COLUMN x6;
CREATE TABLE drop_col_hash (
    x1 int,
    k1 int,
    x2 int,
    k2 int,
    x3 int,
    i1 int,
    x4 int,
    i2 int,
    x5 int,
    v text,
    x6 int,
    PRIMARY KEY (k1 HASH, k2 ASC));
CREATE INDEX ON drop_col_hash (i1 HASH, i2 ASC);
ALTER TABLE drop_col_hash DROP COLUMN x1, DROP COLUMN x2, DROP COLUMN x3, DROP COLUMN x4, DROP COLUMN x5, DROP COLUMN x6;

INSERT INTO drop_col_range (k1, k2, i1, i2, v)
  SELECT i % 4, 1000 - i, (i * 3 + 7) % 8, 2000 - (i * 7 + 3) % 450, 'Value ' || i::text FROM generate_series(1, 900) AS i;
INSERT INTO drop_col_hash SELECT * FROM drop_col_range;

EXPLAIN (COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM drop_col_range WHERE k1 IN (1, 2) ORDER BY k2 LIMIT 10;
SELECT * FROM drop_col_range WHERE k1 IN (1, 2) ORDER BY k2 LIMIT 10;

EXPLAIN (COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM drop_col_range WHERE i1 IN (0, 3, 5, 7) ORDER BY i2 LIMIT 20;
SELECT * FROM drop_col_range WHERE i1 IN (0, 3, 5, 7) ORDER BY i2 LIMIT 20;

EXPLAIN (COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM drop_col_hash WHERE k1 IN (1, 2) ORDER BY k2 LIMIT 10;
SELECT * FROM drop_col_hash WHERE k1 IN (1, 2) ORDER BY k2 LIMIT 10;

EXPLAIN (COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM drop_col_hash WHERE i1 IN (0, 3, 5, 7) ORDER BY i2 LIMIT 20;
SELECT * FROM drop_col_hash WHERE i1 IN (0, 3, 5, 7) ORDER BY i2 LIMIT 20;

DROP TABLE drop_col_range;
DROP TABLE drop_col_hash;
RESET yb_max_merge_scan_streams;
RESET enable_sort;
