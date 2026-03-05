-- -----------------------------------------------------------------------------
-- TEST 1: Orphan Index Entry Check (Failure Rollback)
-- -----------------------------------------------------------------------------
-- Verifies that a statement failure cleaned up index entries.
BEGIN;
CREATE TABLE idx_test (id INT PRIMARY KEY, val INT);
CREATE INDEX ON idx_test(val);

-- Trigger a failure late in the statement.
-- Rows 1-100 will land in Regular DB (if optimized).
INSERT INTO idx_test (id, val)
SELECT i, CASE WHEN i <= 100 THEN i ELSE 1/0 END
FROM generate_series(1, 101) i;

-- On error, the txn is aborted.
ROLLBACK;

-- Verify index is cleaned up. Index-only scan ensures we check the index tablet.
SET enable_seqscan = off;
SELECT count(val) FROM idx_test;
RESET enable_seqscan;

-- -----------------------------------------------------------------------------
-- TEST 2: Multi-Statement Index Visibility
-- -----------------------------------------------------------------------------
-- Verifies that a write to an index is visible to subsequent index-scans in
-- the same transaction (checks HT advancement).
BEGIN;
CREATE TABLE idx_vis_test (id INT PRIMARY KEY, val INT);
CREATE INDEX ON idx_vis_test(val);

-- Statement 1: Write directly to Regular DB
INSERT INTO idx_vis_test VALUES (1, 10), (2, 20);

-- Statement 2: Read via Index
-- Must advance HT to see previous statement's index writes.
SET enable_seqscan = off;
SELECT * FROM idx_vis_test WHERE val = 20;
RESET enable_seqscan;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 3: Unique Constraint Violation (Non-PK)
-- -----------------------------------------------------------------------------
-- Tests the "Read-Before-Write" path. The second insert must see the
-- first insert's data in the Regular DB to trigger the conflict.
BEGIN;

CREATE TABLE unique_test (
    id INT PRIMARY KEY,
    val INT UNIQUE
);

-- First Insert
INSERT INTO unique_test VALUES (1, 100);

-- Second Insert (Must collide with the value in Regular DB)
-- If this succeeds, your HT logic has caused data corruption.
INSERT INTO unique_test VALUES (2, 100);

ROLLBACK;

-- -----------------------------------------------------------------------------
-- TEST 4: Partial Failure in Multi-Row Unique Insert
-- -----------------------------------------------------------------------------
-- Tests statement-level atomicity when a single batch contains a conflict.
BEGIN;
CREATE TABLE unique_multi_test (id INT PRIMARY KEY, val INT UNIQUE);

-- Statement fails on the 3rd row.
-- Rows 1 and 2 must not be visible after the statement fails.
INSERT INTO unique_multi_test VALUES
  (1, 10),
  (2, 20),
  (3, 10);

-- If the statement rollback fails, these might persist.
SELECT count(*) FROM unique_multi_test;

ROLLBACK;
