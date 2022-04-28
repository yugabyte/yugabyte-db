-- Test Setup.
-- Create a range-partitioning hierarchy with multiple keys and default partitions.
CREATE TABLE rp (a int, b int, c varchar, d text, PRIMARY KEY(a,b,c)) PARTITION BY RANGE(a);
CREATE TABLE rp_p1 PARTITION OF rp FOR VALUES FROM (0) TO (100);
CREATE TABLE rp_sub PARTITION OF rp FOR VALUES FROM (100) TO (200) PARTITION BY RANGE (b, c);
CREATE TABLE rp_p2 PARTITION OF rp_sub FOR VALUES FROM (0, 'a') TO (100, 'j');
CREATE TABLE rp_p3 PARTITION OF rp_sub DEFAULT;

-- Create a list-partitioning hierarchy with multiple keys, NULL partitions and default partitions.
CREATE TABLE lp (a int, b int, c varchar, d text) PARTITION BY LIST(a);
CREATE INDEX ON lp(a);
CREATE TABLE lp_p1 PARTITION OF lp FOR VALUES IN (0, 1, 2, 3, 4, 5);
CREATE TABLE lp_sub PARTITION OF lp FOR VALUES IN (6, 7, 8, 9, 10) PARTITION BY LIST (b);
CREATE TABLE lp_p2 PARTITION OF lp_sub FOR VALUES IN (null);
CREATE TABLE lp_p3 PARTITION OF lp_sub FOR VALUES IN (1, 2);

-- Create a non-partitioned table.
CREATE TABLE np (a int, b int, c varchar, d text);

-- Set the number of times custom plans are chosen over generic plans unconditionally.
-- This means that the first time a prepared statement is executed, it will always be
-- executed with a custom plan. Cost comparison between custom and generic plans will
-- take effect at the second invocation.
SET yb_test_planner_custom_plan_threshold=1;

-- Note: The presence of actual values provided to the bound parameters in EXPLAIN
-- output indicates a custom plan. The presence of symbols like '$1' in the EXPLAIN
-- output indicates that a generic plan was chosen.
-- SELECT query where one of the partition key values is a bound parameter and the other is
-- not a bound parameter.
PREPARE t1(int) AS SELECT * FROM rp WHERE a=$1 AND b=3 AND c='1';
EXPLAIN EXECUTE t1(1);
EXPLAIN EXECUTE t1(1);

-- Turn off favoring custom plan over generic plan based on partition pruning.
SET yb_planner_custom_plan_for_partition_pruning=false;
EXPLAIN EXECUTE t1(1);

-- Turn it back on.
SET yb_planner_custom_plan_for_partition_pruning=true;

-- UPDATE list partitioned table using a JOIN with a non-partitioned table.
PREPARE t2(int) AS UPDATE np SET d = 1 FROM lp WHERE lp.a = np.a AND lp.a = $1;
EXPLAIN EXECUTE t2(1);
EXPLAIN EXECUTE t2(1);

-- DELETE range partitioned table using a JOIN with a non-partitioned table.
PREPARE t3(int) AS DELETE FROM rp USING np WHERE rp.a = np.a AND rp.a = $1;
EXPLAIN EXECUTE t3(1);
EXPLAIN EXECUTE t3(1);

-- Subquery test where the outer query has a partition key as a bound parameter.
PREPARE t4 AS SELECT * FROM rp WHERE rp.a = $1 AND rp.b IN (SELECT b FROM np WHERE a < 5) ;
EXPLAIN EXECUTE t4(1);
EXPLAIN EXECUTE t4(1);

-- Subquery test whether the inner query has a partition key as a bound parameter.
PREPARE t5(int) AS SELECT * FROM np WHERE np.a = 1 AND np.b IN (SELECT b FROM lp WHERE a=$1 AND c='1');
EXPLAIN EXECUTE t5(1);
EXPLAIN EXECUTE t5(1);

-- CTE on partitioned tables.
PREPARE t6(int) AS
WITH x AS (UPDATE lp SET d=1 WHERE a = $1 returning a, b, c, d)
SELECT * FROM rp INNER JOIN x ON rp.a = x.a;
EXPLAIN EXECUTE t6(1);
EXPLAIN EXECUTE t6(1);

-- Generic plan must be chosen for SELECTs/UPDATEs/DELETEs if the number of partitions
-- pruned by generic plan is equal to that of custom plan.
PREPARE t7(char) AS SELECT * FROM rp WHERE a=1 AND b=1 AND c=$1;
EXPLAIN EXECUTE t7('a');
EXPLAIN EXECUTE t7('a');

PREPARE t8(char) AS UPDATE lp SET d=3 WHERE a=1 AND b=1 AND c=$1;
EXPLAIN EXECUTE t8('a');
EXPLAIN EXECUTE t8('a');

PREPARE t9(int) AS DELETE FROM np WHERE a < $1;
EXPLAIN EXECUTE t9(1);
EXPLAIN EXECUTE t9(1);
