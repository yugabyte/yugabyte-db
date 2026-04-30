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
