-- Tests for yb_is_local_table: Verify that use of this function in different cases
-- ensures that only data FROM the local region is picked.

-- Test setup.
-- The cluster has been setup with 4 nodes: cloud1.region1.zone1, cloud1.region1.zone2,
-- cloud1.region2.zone2, cloud2.region2.zone2.
-- By default, we connect to the node with cloud1.region1.zone1, therefore tables in
-- cloud1.region1 are considered local.

-- Create two tablespaces, one cloud local and the other region local.
CREATE TABLESPACE regionlocal WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1}]}');
CREATE TABLESPACE cloudlocal WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1}]}');

-- Sanity test with LIST partition hierarchy.
CREATE TABLE list_partitioned (partkey char) PARTITION BY LIST(partkey);
-- Default partition is local.
CREATE TABLE lp_default_local PARTITION OF list_partitioned DEFAULT TABLESPACE regionlocal;
-- Partition without a custom  tablespace.
CREATE TABLE lp_ad_notbsp PARTITION OF list_partitioned FOR VALUES in ('a', 'd');
-- Local partition.
CREATE TABLE lp_bc_local PARTITION OF list_partitioned FOR VALUES in ('b', 'c') TABLESPACE regionlocal;
-- Null partition, remote.
CREATE TABLE lp_null_remote PARTITION OF list_partitioned FOR VALUES in (null) TABLESPACE cloudlocal;
-- Sub-partitions, one local and one remote.
CREATE TABLE lp_efgh PARTITION OF list_partitioned FOR VALUES in ('e', 'f', 'g', 'h') PARTITION BY LIST(partkey);
CREATE TABLE lp_ef_notbsp PARTITION OF lp_efgh FOR VALUES in ('e', 'f');
CREATE TABLE lp_gh_local PARTITION OF lp_efgh FOR VALUES in ('g', 'h') TABLESPACE regionlocal;

EXPLAIN (COSTS OFF) SELECT * FROM list_partitioned WHERE yb_is_local_table(tableoid);

-- Sanity test with HASH partition hierarchy.
CREATE TABLE hash_partitioned (partkey int, partkey2 int) PARTITION BY HASH(partkey);
CREATE TABLE hp0_local PARTITION OF hash_partitioned FOR VALUES WITH (modulus 3, remainder 0) TABLESPACE regionlocal;
CREATE TABLE hp1_remote PARTITION OF hash_partitioned FOR VALUES WITH (modulus 3, remainder 1) TABLESPACE cloudlocal;
-- Sub partitions, one local and the other remote.
CREATE TABLE hp2 PARTITION OF hash_partitioned FOR VALUES WITH (modulus 3, remainder 2) PARTITION BY HASH(partkey);
CREATE TABLE hp2_1_local PARTITION OF hp2 FOR VALUES WITH (modulus 2, remainder 0) TABLESPACE regionlocal;
CREATE TABLE hp2_2_notbsp PARTITION OF hp2 FOR VALUES WITH (modulus 2, remainder 1);

EXPLAIN (COSTS OFF) SELECT * FROM hash_partitioned WHERE yb_is_local_table(tableoid);

-- Sanity test with RANGE partition hierarchy.
CREATE TABLE range_partitioned (partkey int, partvalue int) PARTITION BY RANGE (partkey);
-- Remote default partition.
CREATE TABLE rp_default_remote PARTITION OF range_partitioned DEFAULT TABLESPACE cloudlocal;
-- One local and one remote partition.
CREATE TABLE rp_10_15_remote PARTITION OF range_partitioned FOR VALUES FROM (10) TO (15) TABLESPACE cloudlocal;
CREATE TABLE rp_15_20_local PARTITION OF range_partitioned FOR VALUES FROM (15) TO (20) TABLESPACE regionlocal;
--Sub-partitions, one local and one remote.
CREATE TABLE rp_1_10 PARTITION OF range_partitioned FOR VALUES FROM (1) TO (10) PARTITION BY RANGE(partkey);
CREATE TABLE rp_1_5_notbsp PARTITION OF rp_1_10 FOR VALUES FROM (1) TO (5);
CREATE TABLE rp_5_10_local PARTITION OF rp_1_10 FOR VALUES FROM (5) TO (10) TABLESPACE regionlocal;

EXPLAIN (COSTS OFF) SELECT * FROM range_partitioned WHERE yb_is_local_table(tableoid);

-- Sanity test with JOIN across partition hierarchies.
-- The function is invoked only across hash_partitioned, so scan all partitions of
-- range_partitioned, but only local partitions of hash_partitioned.
EXPLAIN (COSTS OFF) SELECT * FROM range_partitioned INNER JOIN hash_partitioned ON range_partitioned.partkey = hash_partitioned.partkey WHERE yb_is_local_table(hash_partitioned.tableoid);

-- TODO (deepthi): The following query should prune out the local partition but it does not.
-- Fix this in a later patch.
EXPLAIN (COSTS OFF) SELECT * FROM range_partitioned WHERE NOT yb_is_local_table(tableoid);

-- TODO (deepthi): The following query should result in an IndexOnly scan but currently it does not.
-- Fix this in a later patch.
CREATE INDEX rp_idx ON range_partitioned(partvalue);
-- Because of varying costs, sometimes seq scan may be picked instead of IndexScan. For the purposes
-- of this test, it does not matter either way. Disable seq scan for this test to make the test
-- deterministic.
SET enable_seqscan=false;
EXPLAIN (COSTS OFF) SELECT partvalue FROM range_partitioned WHERE yb_is_local_table(tableoid) AND partvalue = 5;
SET enable_seqscan=true;

-- Update test.
-- TODO (deepthi): The following query must invoke the fast path for update as it affects only
-- one partition. Fix constraint exclusion to also consider yb_is_local_table.
EXPLAIN (COSTS OFF) UPDATE range_partitioned SET partvalue = 5 WHERE yb_is_local_table(tableoid);
EXPLAIN (COSTS OFF) UPDATE list_partitioned SET partkey = 5 WHERE yb_is_local_table(tableoid);
EXPLAIN (COSTS OFF) UPDATE hash_partitioned SET partkey = 5 WHERE yb_is_local_table(tableoid);

-- Sanity test for multi-key partitioning.
CREATE TABLE multikey (a int, b int) PARTITION BY RANGE (a, b);
CREATE TABLE multikey_default_local PARTITION OF multikey DEFAULT TABLESPACE regionlocal;
CREATE TABLE multikey0 PARTITION OF multikey FOR VALUES FROM (minvalue, minvalue) TO (1, minvalue) TABLESPACE cloudlocal;
CREATE TABLE multikey1_local PARTITION OF multikey FOR VALUES FROM (1, minvalue) TO (1, 1) TABLESPACE regionlocal;
CREATE TABLE multikey2 PARTITION OF multikey FOR VALUES FROM (1, 1) TO (2, minvalue);
CREATE TABLE multikey3_local PARTITION OF multikey FOR VALUES FROM (2, minvalue) TO (2, 1) TABLESPACE regionlocal;
CREATE TABLE multikey4 PARTITION OF multikey FOR VALUES FROM (2, 1) TO (2, maxvalue) TABLESPACE cloudlocal;
CREATE TABLE multikey5 PARTITION OF multikey FOR VALUES FROM (2, maxvalue) TO (maxvalue, maxvalue);

-- Query with one partition key and function.
EXPLAIN (COSTS OFF) SELECT * FROM multikey WHERE a < 2 AND yb_is_local_table(tableoid);
-- Query with both partition keys.
EXPLAIN (COSTS OFF) SELECT * FROM multikey WHERE a = 2 AND b < 1 AND yb_is_local_table(tableoid);
-- Query with null check.
EXPLAIN (COSTS OFF) SELECT * FROM multikey WHERE b is null AND yb_is_local_table(tableoid);

-- Sanity Test for 3 types of JOINs.
-- Create prt1, a partition hierarchy with multiples of 20 in the range 0 to 600.
CREATE TABLE prt1 (a int) PARTITION BY RANGE(a);
CREATE TABLE prt1_p1 PARTITION OF prt1 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_p2_local PARTITION OF prt1 FOR VALUES FROM (250) TO (500) TABLESPACE regionlocal;
CREATE TABLE prt1_p3 PARTITION OF prt1 FOR VALUES FROM (500) TO (600);
INSERT INTO prt1 SELECT i FROM generate_series(0, 599) i WHERE i % 20 = 0;

-- Create prt2 with same partition hierarchy as prt1 but with multiples of 30.
CREATE TABLE prt2 ( b int) PARTITION BY RANGE(b);
CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_p2_local PARTITION OF prt2 FOR VALUES FROM (250) TO (500) TABLESPACE regionlocal;
CREATE TABLE prt2_p3 PARTITION OF prt2 FOR VALUES FROM (500) TO (600);
INSERT INTO prt2 SELECT i FROM generate_series(0, 599) i WHERE i % 30 = 0;

-- Inner Join. Pick local tables with a < 450.
EXPLAIN (COSTS OFF)
SELECT t1.a, t2.b FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.a < 450 AND yb_is_local_table(t1.tableoid) ORDER BY t1.a, t2.b;
SELECT t1.a, t2.b FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.a < 450 AND yb_is_local_table(t1.tableoid) ORDER BY t1.a, t2.b;

-- Left Join.
EXPLAIN (COSTS OFF)
SELECT t1.a, t2.b FROM (SELECT * FROM prt1 WHERE a < 450) t1 LEFT JOIN (SELECT * FROM prt2 WHERE yb_is_local_table(prt2.tableoid)) t2 ON t1.a = t2.b ORDER BY t1.a, t2.b;
SELECT t1.a, t2.b FROM (SELECT * FROM prt1 WHERE a < 450) t1 LEFT JOIN (SELECT * FROM prt2 WHERE yb_is_local_table(prt2.tableoid)) t2 ON t1.a = t2.b ORDER BY t1.a, t2.b;

-- Full Join.
EXPLAIN (COSTS OFF)
SELECT t1.a, t2.b FROM (SELECT * FROM prt1 WHERE yb_is_local_table(prt1.tableoid)) t1 FULL JOIN (SELECT * FROM prt2 WHERE b > 250) t2 ON t1.a = t2.b ORDER BY t1.a, t2.b;
SELECT t1.a, t2.b FROM (SELECT * FROM prt1 WHERE yb_is_local_table(prt1.tableoid)) t1 FULL JOIN (SELECT * FROM prt2 WHERE b > 250) t2 ON t1.a = t2.b ORDER BY t1.a, t2.b;

-- Semi-join
EXPLAIN (COSTS OFF)
SELECT t1.* FROM prt1 t1 WHERE t1.a IN (SELECT t2.b FROM prt2 t2 WHERE yb_is_local_table(t2.tableoid)) ORDER BY t1.a;

-- TEMP tables.
CREATE TEMP TABLE temp_partitioned (partkey char) PARTITION BY LIST(partkey);
CREATE TEMP TABLE tp1 PARTITION OF temp_partitioned FOR VALUES in ('a', 'd');
CREATE TEMP TABLE tp2 PARTITION OF temp_partitioned FOR VALUES in ('b', 'c');

EXPLAIN (COSTS OFF) SELECT * FROM temp_partitioned WHERE yb_is_local_table(tableoid);

-- Cleanup.
DROP TABLE range_partitioned;
DROP TABLE list_partitioned;
DROP TABLE hash_partitioned;
DROP TABLE multikey;
DROP TABLE prt1;
DROP TABLE prt2;
DROP TABLE temp_partitioned;
DROP TABLESPACE regionlocal;
DROP TABLESPACE cloudlocal;
