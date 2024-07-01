SET compute_query_id to regress; -- hide query id for EXPLAIN VERBOSE

-- Check for unsupported system columns.
CREATE TABLE test_tab1(id INT);
INSERT INTO test_tab1 VALUES (1) RETURNING ctid;
INSERT INTO test_tab1 VALUES (2) RETURNING cmin;
INSERT INTO test_tab1 VALUES (3) RETURNING cmax;
INSERT INTO test_tab1 VALUES (4) RETURNING xmin;
INSERT INTO test_tab1 VALUES (5) RETURNING xmax;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab1;
SELECT ctid FROM test_tab1;
SELECT cmin FROM test_tab1;
SELECT cmax FROM test_tab1;
SELECT xmin FROM test_tab1;
SELECT xmax FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab1 WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab1 WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab1 WHERE id = 3;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab1 WHERE id = 4;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab1 WHERE id = 5;
SELECT ctid FROM test_tab1 WHERE id = 1;
SELECT cmin FROM test_tab1 WHERE id = 2;
SELECT cmax FROM test_tab1 WHERE id = 3;
SELECT xmin FROM test_tab1 WHERE id = 4;
SELECT xmax FROM test_tab1 WHERE id = 5;
-- With primary key.
CREATE TABLE test_tab2(id INT, PRIMARY KEY(id));
INSERT INTO test_tab2 VALUES (1) RETURNING ctid;
INSERT INTO test_tab2 VALUES (2) RETURNING cmin;
INSERT INTO test_tab2 VALUES (3) RETURNING cmax;
INSERT INTO test_tab2 VALUES (4) RETURNING xmin;
INSERT INTO test_tab2 VALUES (5) RETURNING xmax;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab2 WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab2 WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab2 WHERE id = 3;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab2 WHERE id = 4;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab2 WHERE id = 5;
SELECT ctid FROM test_tab2 WHERE id = 1;
SELECT cmin FROM test_tab2 WHERE id = 2;
SELECT cmax FROM test_tab2 WHERE id = 3;
SELECT xmin FROM test_tab2 WHERE id = 4;
SELECT xmax FROM test_tab2 WHERE id = 5;
-- All system columns should work for temp TABLE.
CREATE temp TABLE test_temp_tab(id INT, PRIMARY KEY(id));
INSERT INTO test_temp_tab VALUES (1) RETURNING ctid;
INSERT INTO test_temp_tab VALUES (2) RETURNING cmin;
INSERT INTO test_temp_tab VALUES (3) RETURNING cmax;
/* YB TODO: uncomment with issue #17154.
-- Since xmin and xmax output can easily change, don't directly output them.
WITH yb_with AS (
INSERT INTO test_temp_tab VALUES (4) RETURNING xmin
) SELECT count(*) FROM yb_with;
WITH yb_with AS (
INSERT INTO test_temp_tab VALUES (5) RETURNING xmax
) SELECT count(*) FROM yb_with;
*/
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_temp_tab;
-- Since xmin and xmax output can easily change, don't directly output them.
EXPLAIN (VERBOSE, COSTS OFF)
WITH yb_with AS (
SELECT xmin FROM test_temp_tab
) SELECT count(*) FROM yb_with;
EXPLAIN (VERBOSE, COSTS OFF)
WITH yb_with AS (
SELECT xmax FROM test_temp_tab
) SELECT count(*) FROM yb_with;
SELECT ctid FROM test_temp_tab;
SELECT cmin FROM test_temp_tab;
SELECT cmax FROM test_temp_tab;
-- Since xmin and xmax output can easily change, don't directly output them.
WITH yb_with AS (
SELECT xmin FROM test_temp_tab
) SELECT count(*) FROM yb_with;
WITH yb_with AS (
SELECT xmax FROM test_temp_tab
) SELECT count(*) FROM yb_with;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_temp_tab WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_temp_tab WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_temp_tab WHERE id = 3;
-- Since xmin and xmax output can easily change, don't directly output them.
EXPLAIN (VERBOSE, COSTS OFF)
WITH yb_with AS (
SELECT xmin FROM test_temp_tab WHERE id = 4
) SELECT count(*) FROM yb_with;
EXPLAIN (VERBOSE, COSTS OFF)
WITH yb_with AS (
SELECT xmax FROM test_temp_tab WHERE id = 5
) SELECT count(*) FROM yb_with;
SELECT ctid FROM test_temp_tab WHERE id = 1;
SELECT cmin FROM test_temp_tab WHERE id = 2;
SELECT cmax FROM test_temp_tab WHERE id = 3;
-- Since xmin and xmax output can easily change, don't directly output them.
WITH yb_with AS (
SELECT xmin FROM test_temp_tab WHERE id = 4
) SELECT count(*) FROM yb_with;
WITH yb_with AS (
SELECT xmax FROM test_temp_tab WHERE id = 5
) SELECT count(*) FROM yb_with;
