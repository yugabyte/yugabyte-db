-- -----------------------------------------------------------------------------
-- TEST 1: COPY Failure and Rollback
-- -----------------------------------------------------------------------------
BEGIN;
CREATE TABLE copy_rollback_test (id INT PRIMARY KEY, name TEXT);

-- Attempt a copy that fails halfway due to a malformed integer
-- Use a temporary table or file-based simulation
COPY copy_rollback_test FROM STDIN;
1	Alice
2	Bob
three	Charlie
\.

-- On failure, everything should be cleaned up.
ROLLBACK;

SELECT count(*) FROM copy_rollback_test;

-- -----------------------------------------------------------------------------
-- TEST 2: Successful COPY Visibility
-- -----------------------------------------------------------------------------
BEGIN;
CREATE TABLE copy_vis_test (id INT PRIMARY KEY);

COPY copy_vis_test FROM STDIN;
1
2
3
\.

-- Verify immediate visibility after COPY
SELECT count(*) FROM copy_vis_test;

COMMIT;
