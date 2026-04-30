--
-- Yugabyte-owned file for testing DML queries that do not fall into more
-- specific buckets (yb_dml_*.sql).
--

-- GH-22967: Test that a Value Scan fed by a Sub Plan does not cause an INSERT
-- to crash. The query relies on the fact the EXISTS .. IN expression does not
-- get constant-folded and is evaluated in its own Sub Plan.
CREATE TABLE GH_22967 (k INT4 PRIMARY KEY);
EXPLAIN (COSTS OFF) INSERT INTO GH_22967 VALUES ((EXISTS(SELECT 1) in (SELECT true))::INT4), (-10);
INSERT INTO GH_22967 VALUES ((EXISTS(SELECT 1) in (SELECT true))::INT4), (-10);


-- Test that foreign key constraints are enforced
CREATE TABLE customers (
    customer_id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL
);

CREATE TABLE orders (
    order_id SERIAL PRIMARY KEY,
    order_date DATE NOT NULL,
    amount DECIMAL(10, 2) NOT NULL,
    customer_id INTEGER NOT NULL,
    FOREIGN KEY (customer_id) REFERENCES customers(customer_id)
);

INSERT INTO customers (name, email) VALUES
('Alice Johnson', 'alice@example.com'),
('Bob Smith', 'bob@example.com');

INSERT INTO orders (order_date, amount, customer_id) VALUES
('2023-10-01', 250.00, 1),
('2023-10-02', 450.50, 2);

-- Attempt to insert an order with a non-existent customer
INSERT INTO orders (order_date, amount, customer_id) VALUES
('2023-10-03', 300.00, 3);

-- Attempt to delete a customer that still has orders
DELETE FROM customers WHERE customer_id = 2;

-- Test cascading deletes
DROP TABLE orders;
CREATE TABLE orders_cascade (
    order_id SERIAL PRIMARY KEY,
    order_date DATE NOT NULL,
    amount DECIMAL(10, 2) NOT NULL,
    customer_id INTEGER NOT NULL,
    FOREIGN KEY (customer_id) REFERENCES customers(customer_id) ON DELETE CASCADE
);
INSERT INTO orders_cascade (order_date, amount, customer_id) VALUES
('2023-10-01', 250.00, 1),
('2023-10-02', 450.50, 2);
DELETE FROM customers WHERE customer_id = 2;
SELECT * FROM orders_cascade;


-- Test adding foreign key constraint using ALTER TABLE ADD CONSTRAINT
CREATE TABLE customers_test (
    customer_id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL
);

CREATE TABLE orders_test (
    order_id SERIAL PRIMARY KEY,
    customer_id INTEGER,
    name VARCHAR(100) NOT NULL
);

-- Add foreign key constraint using ALTER TABLE
ALTER TABLE orders_test
ADD CONSTRAINT fk_orders_customers
FOREIGN KEY (customer_id) REFERENCES customers_test(customer_id);

-- Insert valid data
INSERT INTO customers_test (name) VALUES ('Customer 1'), ('Customer 2');
INSERT INTO orders_test (customer_id, name) VALUES (1, 'Order 1'), (2, 'Order 2');

-- Attempt to insert a child with a non-existent customer
INSERT INTO orders_test (customer_id, name) VALUES (3, 'Order 3');

-- Attempt to delete a customer that still has orders
DELETE FROM customers_test WHERE customer_id = 2;

-- Test that invalidation of the foreign key cache works
ALTER TABLE orders_test DROP CONSTRAINT fk_orders_customers;
INSERT INTO orders_test (customer_id, name) VALUES (3, 'Order 3');

-- GH-26464: Test deleting rows across multiple partitions of a partitioned table
CREATE TABLE base (k INT PRIMARY KEY, v INT) PARTITION BY RANGE (k);
CREATE TABLE part1 PARTITION OF base FOR VALUES FROM (11) TO (21);
CREATE TABLE part2 PARTITION OF base FOR VALUES FROM (21) TO (31);
CREATE TABLE part3 PARTITION OF base FOR VALUES FROM (31) TO (41);
CREATE TABLE part4 (k INT PRIMARY KEY, v INT) PARTITION BY RANGE (k);
CREATE TABLE part4a PARTITION OF part4 FOR VALUES FROM (41) TO (46);
CREATE TABLE part4b PARTITION OF part4 FOR VALUES FROM (46) TO (51);
ALTER TABLE base ATTACH PARTITION part4 FOR VALUES FROM (41) TO (51);

CREATE INDEX ON part3 (v);
CREATE INDEX ON part4b (v);

INSERT INTO base VALUES (11, 11), (22, 22), (23, 23), (33, 33), (34, 44), (44, 44), (47 ,47);

-- Two partitions that individually satisfy single shard constraints.
DELETE FROM base WHERE k IN (11, 22);
-- One satisfies the single shard constraint, the other does not.
DELETE FROM base WHERE k IN (23, 33);
-- Partitions that are on different levels of the tree, some of which satisfy
-- single shard constraints.
DELETE FROM base WHERE k IN (34, 44, 47);

SELECT * FROM base ORDER BY k;
DROP TABLE base CASCADE;

-- GH-30373
-- The scans set "reference row marks" which should NOT be explained as part of EXPLAIN's output.
CREATE TABLE gh30373 (id INT PRIMARY KEY);
INSERT INTO gh30373 VALUES(1);
BEGIN ISOLATION LEVEL SERIALIZABLE;
EXPLAIN (ANALYZE ON) UPDATE gh30373 a SET id = b.id+1 FROM gh30373 b WHERE a.id=b.id;
ROLLBACK;

-- Test B: yb_enable_upsert_mode + INSERT on table with secondary index
-- Conflicting INSERT gets duplicate key error; non-conflicting INSERT succeeds.
create table upsert_guard_test2 (h int primary key, v1 int, v2 int);
create index upsert_guard_idx2 on upsert_guard_test2 (v1);
insert into upsert_guard_test2 values (1, 10, 100), (2, 20, 200);

set yb_enable_upsert_mode = true;
-- conflicting PK: fails with duplicate key error
insert into upsert_guard_test2 values (1, 11, 111);
-- non-conflicting PK: succeeds (upsert mode silently disabled)
insert into upsert_guard_test2 values (3, 30, 300);
set yb_enable_upsert_mode = false;

-- Original rows intact, plus the non-conflicting row was inserted.
select * from upsert_guard_test2 order by h;

-- Index should be consistent with base table.
/*+IndexOnlyScan(upsert_guard_test2 upsert_guard_idx2)*/
select v1 from upsert_guard_test2 where v1 > 0 order by v1;

-- Test D: BEFORE ROW trigger
-- Upsert mode should be disabled; trigger should fire on insert so the audit
-- row reflects the insert.
create table upsert_guard_test4 (h int primary key, v1 int);
create table upsert_guard_audit4 (event text, h int);
create function upsert_guard_trig4() returns trigger as $$
begin
    insert into upsert_guard_audit4 values (tg_op || ' BEFORE', new.h);
    return new;
end;
$$ language plpgsql;
create trigger upsert_guard_trig4_b before insert on upsert_guard_test4
    for each row execute function upsert_guard_trig4();
insert into upsert_guard_test4 values (1, 10);

set yb_enable_upsert_mode = true;
insert into upsert_guard_test4 values (2, 20);
set yb_enable_upsert_mode = false;

-- Base table has both rows; audit has BEFORE INSERT entries for both.
select * from upsert_guard_test4 order by h;
select * from upsert_guard_audit4 order by h;

-- Test E: AFTER ROW trigger
-- Same as Test D but with AFTER INSERT trigger.
create table upsert_guard_test5 (h int primary key, v1 int);
create table upsert_guard_audit5 (event text, h int);
create function upsert_guard_trig5() returns trigger as $$
begin
    insert into upsert_guard_audit5 values (tg_op || ' AFTER', new.h);
    return new;
end;
$$ language plpgsql;
create trigger upsert_guard_trig5_a after insert on upsert_guard_test5
    for each row execute function upsert_guard_trig5();
insert into upsert_guard_test5 values (1, 10);

set yb_enable_upsert_mode = true;
insert into upsert_guard_test5 values (2, 20);
set yb_enable_upsert_mode = false;

select * from upsert_guard_test5 order by h;
select * from upsert_guard_audit5 order by h;

-- Test F: CHECK constraint (unaffected by the guardrail)
-- CHECK constraints do not use triggers, so upsert mode should still work.
-- Upsert on conflicting PK should overwrite the row (blind write).
create table upsert_guard_test6 (h int primary key, v1 int check (v1 >= 0));
insert into upsert_guard_test6 values (1, 10);

set yb_enable_upsert_mode = true;
insert into upsert_guard_test6 values (1, 99);
set yb_enable_upsert_mode = false;

-- Upsert succeeded, v1 was replaced.
select * from upsert_guard_test6 order by h;

-- Test G: Generated column (unaffected by the guardrail)
-- Generated columns do not use triggers, so upsert mode should still work.
create table upsert_guard_test7 (h int primary key, v1 int,
                                 v2 int generated always as (v1 * 2) stored);
insert into upsert_guard_test7 (h, v1) values (1, 10);

set yb_enable_upsert_mode = true;
insert into upsert_guard_test7 (h, v1) values (1, 99);
set yb_enable_upsert_mode = false;

-- Upsert succeeded, v1 replaced and v2 regenerated.
select * from upsert_guard_test7 order by h;

-- clean up
DROP TABLE upsert_guard_test2;
DROP TABLE upsert_guard_test4;
DROP TABLE upsert_guard_audit4;
DROP FUNCTION upsert_guard_trig4();
DROP TABLE upsert_guard_test5;
DROP TABLE upsert_guard_audit5;
DROP FUNCTION upsert_guard_trig5();
DROP TABLE upsert_guard_test6;
DROP TABLE upsert_guard_test7;

-- GH-31214 - validate single shard updates on various partition table hierarchies.
-- Case 1: Root and leaf share the same primary key (k1 - cluster key, k2 - partition key).
-- So, the partition key (k2) is a subset of the primary key (k1, k2).
CREATE TABLE root_pk_base (k1 INT, k2 INT, v INT, PRIMARY KEY (k1, k2)) PARTITION BY RANGE (k2);
CREATE TABLE root_pk_p1 PARTITION OF root_pk_base FOR VALUES FROM (1) TO (11);
CREATE TABLE root_pk_p2 PARTITION OF root_pk_base FOR VALUES FROM (11) TO (21);
INSERT INTO root_pk_base VALUES (1, 1, 1), (2, 2, 2), (13, 13, 13);

-- Single shard update that does not violate the partition constraint.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE root_pk_p1 SET v = v + 1 WHERE k1 = 1 AND k2 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE root_pk_p1 SET v = v + 1, k2 = k2 + 1 WHERE k1 = 1 AND k2 = 1;

-- Single shard update that violates the partition constraint.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE root_pk_p1 SET k2 = 12 WHERE k1 = 2 AND k2 = 2;

SELECT * FROM root_pk_base ORDER BY k1, k2;
DROP TABLE root_pk_base CASCADE;

-- Case 2: No PK on root or leaves. All updates are distributed transactions since neither table has
-- a primary key.
CREATE TABLE no_pk_base (v1 INT, v2 INT) PARTITION BY RANGE (v1);
CREATE TABLE no_pk_p1 PARTITION OF no_pk_base FOR VALUES FROM (1) TO (11);
CREATE TABLE no_pk_p2 PARTITION OF no_pk_base FOR VALUES FROM (11) TO (21);
INSERT INTO no_pk_base VALUES (1, 1), (2, 2), (14, 14), (15, 15);

-- Updates addressing the leaf partition.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_p1 SET v2 = v2 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_p1 SET v1 = 3 WHERE v1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_p1 SET v1 = 12 WHERE v1 = 3; -- this should fail the partition constraint

-- Updates addressing the root partition.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_base SET v2 = v2 + 1 WHERE v1 = 14;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_base SET v1 = 16 WHERE v1 = 15;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pk_base SET v1 = 5 WHERE v1 = 16;

SELECT * FROM no_pk_base ORDER BY v1;
DROP TABLE no_pk_base CASCADE;

-- Case 3: Root has no primary key. Leaves have a primary key that serves as the clustering key.
-- The clustering key (k) equals the partitioning key (k).
CREATE TABLE ck_base (k INT, v INT) PARTITION BY RANGE (k);
CREATE TABLE ck_p1 PARTITION OF ck_base (k PRIMARY KEY) FOR VALUES FROM (1) TO (11);
CREATE TABLE ck_p2 PARTITION OF ck_base (k PRIMARY KEY) FOR VALUES FROM (11) TO (21);
INSERT INTO ck_base VALUES (1, 1), (12, 12);

-- Single shard updates by addressing the leaf.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_p1 SET v = v + 1 WHERE k = 1;
-- Updates modifying the clustering/partitioning key.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_p1 SET k = 2 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_p1 SET k = 16 WHERE k = 2; -- this should fail the partition constraint

-- Single shard updates by addressing the root.
-- The planner can determine the destination leaf via partition pruning on k, and the
-- leaf's clustering key is fully specified.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_base SET v = v + 1 WHERE k = 12;

SELECT * FROM ck_base ORDER BY k;
DROP TABLE ck_base CASCADE;

-- Case 4: Root has no primary key. Leaves have a primary key that serves as the clustering key.
-- The clustering key (ck) differs from the partitioning key (ptk).
-- ck_ne_ptk = clustering key not equal to partition key.
CREATE TABLE ck_ne_ptk_base (ck INT, ptk INT, v INT) PARTITION BY RANGE (ptk);
CREATE TABLE ck_ne_ptk_p1 PARTITION OF ck_ne_ptk_base (ck PRIMARY KEY) FOR VALUES FROM (1) TO (11);
CREATE TABLE ck_ne_ptk_p2 PARTITION OF ck_ne_ptk_base (ck PRIMARY KEY) FOR VALUES FROM (11) TO (21);
INSERT INTO ck_ne_ptk_base VALUES (1, 5, 1), (2, 5, 2), (3, 15, 3), (4, 5, 4), (5, 5, 5), (6, 15, 6), (7, 5, 7), (8, 5, 8);

-- 4a. Cross-partition update via leaf with clustering key in WHERE. Should fail the partition constraint.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ne_ptk_p1 SET ptk = 16 WHERE ck = 1;
SELECT * FROM ck_ne_ptk_p1 ORDER BY ck;
SELECT * FROM ck_ne_ptk_p2 ORDER BY ck;

-- 4b. Cross-partition update via leaf without clustering key in WHERE. Should fail the partition constraint.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ne_ptk_p1 SET ptk = 17 WHERE ptk = 5;
SELECT * FROM ck_ne_ptk_base ORDER BY ck;

-- 4c. Same-partition clustering key update via leaf. (distributed transaction).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ne_ptk_p2 SET ck = 10 WHERE ck = 3;

-- 4d. Non-partition column update via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ne_ptk_p1 SET v = v + 1 WHERE ck = 7;

-- 4e. Partition key update to a value that stays in the same partition (distributed transaction).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ne_ptk_p1 SET ptk = 6 WHERE ck = 8;

SELECT * FROM ck_ne_ptk_base ORDER BY ck;
DROP TABLE ck_ne_ptk_base CASCADE;

-- Case 5: Multi-column partition key with a separate leaf clustering key.
-- Root has no primary key. Leaves have clustering key (ck) that differs from the
-- multi-column partition key (ptk1, ptk2).
CREATE TABLE ck_mc_ptk_base (ck INT, ptk1 INT, ptk2 INT, v INT) PARTITION BY RANGE (ptk1, ptk2);
CREATE TABLE ck_mc_ptk_p1 PARTITION OF ck_mc_ptk_base (ck PRIMARY KEY) FOR VALUES FROM (1, 1) TO (11, 11);
INSERT INTO ck_mc_ptk_base VALUES (1, 5, 5, 1);

-- Update one partition key column (ptk1) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_mc_ptk_p1 SET ptk1 = 6 WHERE ck = 1;

-- Update all partition key columns (ptk1, ptk2) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_mc_ptk_p1 SET ptk1 = 7, ptk2 = 7 WHERE ck = 1;

-- Update only a non-partition column (v) via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_mc_ptk_p1 SET v = v + 1 WHERE ck = 1;

SELECT * FROM ck_mc_ptk_base ORDER BY ck;
DROP TABLE ck_mc_ptk_base CASCADE;

-- Case 6: Partition key overlaps with but is not a subset of the clustering key.
-- Clustering key = (ck1, ck2), Partition key = (ck1, ptk).
CREATE TABLE ck_ov_ptk_base (ck1 INT, ck2 INT, ptk INT, v INT) PARTITION BY RANGE (ck1, ptk);
CREATE TABLE ck_ov_ptk_p1 PARTITION OF ck_ov_ptk_base FOR VALUES FROM (1, 1) TO (11, 11);
ALTER TABLE ck_ov_ptk_p1 ADD PRIMARY KEY (ck1, ck2);
INSERT INTO ck_ov_ptk_base VALUES (1, 1, 5, 1);

-- Update partition-key-only column (ptk) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ov_ptk_p1 SET ptk = 6 WHERE ck1 = 1 AND ck2 = 1;

-- Update clustering key column (ck2) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ov_ptk_p1 SET ck2 = 6 WHERE ck1 = 1 AND ck2 = 1;

-- Update non-key column (v) via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ck_ov_ptk_p1 SET v = v + 1 WHERE ck1 = 1 AND ck2 = 1;

SELECT * FROM ck_ov_ptk_base ORDER BY ck1, ck2;
DROP TABLE ck_ov_ptk_base CASCADE;

-- Case 7: Multi-level partition hierarchy (root -> mid-level -> leaf).
-- Root is partitioned by ptk1, mid-level is partitioned by ptk2, leaf has clustering key ck
-- which differs from both partition keys.
CREATE TABLE ml_root (ck INT, ptk1 INT, ptk2 INT, v INT) PARTITION BY RANGE (ptk1);
CREATE TABLE ml_mid PARTITION OF ml_root FOR VALUES FROM (1) TO (11) PARTITION BY RANGE (ptk2);
CREATE TABLE ml_leaf PARTITION OF ml_mid (ck PRIMARY KEY) FOR VALUES FROM (1) TO (11);
INSERT INTO ml_root VALUES (1, 5, 5, 1);

-- Update mid-level partition key (ptk2) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ml_leaf SET ptk2 = 6 WHERE ck = 1;

-- Update root partition key (ptk1) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ml_leaf SET ptk1 = 6 WHERE ck = 1;

-- Update non-partition column (v) via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ml_leaf SET v = v + 1 WHERE ck = 1;

-- Update root partition key (ptk1) addressing the mid-level partition (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ml_mid SET ptk1 = 7 WHERE ck = 1;

SELECT * FROM ml_root ORDER BY ck;
DROP TABLE ml_root CASCADE;

-- Case 8: Expression-based partition keys.
CREATE TABLE expr_ptk_base (ck INT, a INT, b1 TEXT, b2 TEXT, v INT) PARTITION BY RANGE (lower(b1), a, lower(b2));
CREATE TABLE expr_ptk_p1 PARTITION OF expr_ptk_base (ck PRIMARY KEY) FOR VALUES FROM ('aaa', 1, 'aaa') TO ('zzz', 100, 'zzz');
INSERT INTO expr_ptk_base VALUES (1, 5, 'hello', 'world', 1);

-- Update regular partition key column (a) via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_ptk_p1 SET a = 6 WHERE ck = 1;

-- Update column referenced by the first expression key via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_ptk_p1 SET b1 = 'goodbye' WHERE ck = 1;

-- Update column referenced by the third expression key via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_ptk_p1 SET b2 = 'goodbye' WHERE ck = 1;

-- Update non-key column (v) via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_ptk_p1 SET v = v + 1 WHERE ck = 1;

SELECT * FROM expr_ptk_base ORDER BY ck;
DROP TABLE expr_ptk_base CASCADE;

-- Case 9: ATTACH PARTITION with different column ordering between parent and leaf.
CREATE TABLE out_of_order_cols_base (ck INT, ptk1 INT, ptk2 TEXT, v INT) PARTITION BY RANGE (ptk1, ptk2);
CREATE TABLE out_of_order_cols_p1 (ptk2 TEXT, v INT, ptk1 INT, ck INT PRIMARY KEY);
ALTER TABLE out_of_order_cols_base ATTACH PARTITION out_of_order_cols_p1 FOR VALUES FROM (1, 'a') TO (11, 'z');
INSERT INTO out_of_order_cols_base VALUES (1, 5, 'hello', 1), (2, 6, 'bar', 123);

-- Update ptk1 via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE out_of_order_cols_p1 SET ptk1 = 6 WHERE ck = 1;

-- Update ptk2 via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE out_of_order_cols_p1 SET ptk2 = 'goodbye' WHERE ck = 1;

-- Update non-key column (v) via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE out_of_order_cols_p1 SET v = v + 1 WHERE ck = 1;

SELECT * FROM out_of_order_cols_base ORDER BY ck;
DROP TABLE out_of_order_cols_base CASCADE;

-- Case 10: Expression partition key + with different column ordering between parent and leaf.
CREATE TABLE expr_mixed_base (ck INT, n INT, b TEXT, v INT) PARTITION BY RANGE (lower(b), abs(n));
CREATE TABLE expr_mixed_p1 (b TEXT, v INT, n INT, ck INT PRIMARY KEY);
ALTER TABLE expr_mixed_base ATTACH PARTITION expr_mixed_p1 FOR VALUES FROM ('aaa', 1) TO ('zzz', 100);
INSERT INTO expr_mixed_base VALUES (1, 5, 'hello', 1);

-- Update column referenced by TEXT expression key via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_mixed_p1 SET b = 'goodbye' WHERE ck = 1;

-- Update olumn referenced by INT expression key via leaf (distributed).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_mixed_p1 SET n = 6 WHERE ck = 1;

-- Update non-key column v via leaf (single shard).
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE expr_mixed_p1 SET v = v + 1 WHERE ck = 1;

SELECT * FROM expr_mixed_base ORDER BY ck;
DROP TABLE expr_mixed_base CASCADE;
