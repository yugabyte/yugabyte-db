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

-- GH-25911: Test constraint behavior on row movement between partitions
--New Scenario: CHECK constraint on partitionB
CREATE TABLE check_part_test (partid INT, val INT) PARTITION BY RANGE (partid);
CREATE TABLE part_a PARTITION OF check_part_test FOR VALUES FROM (0) TO (10);
CREATE TABLE part_b PARTITION OF check_part_test default;
-- Constraint CHECK on partition B
ALTER TABLE part_b ADD CONSTRAINT part_b_value_check CHECK (val < 100);

-- Insert a row into partition A
INSERT INTO check_part_test VALUES (1, 50);

-- Attempt to move to partition B where value fails constraint CHECK
-- Expected: ERROR
UPDATE check_part_test SET partid = 20, val = 900 WHERE partid = 1;

SELECT * FROM part_a;
SELECT * FROM part_b;


-- New scenario: CHECK constraint on partitionA
-- Clean slate
TRUNCATE check_part_test;

-- Remove any existing CHECK constraint from part_b
ALTER TABLE part_b DROP CONSTRAINT IF EXISTS part_b_value_check;
-- Add CHECK constraint on part_a
ALTER TABLE part_a ADD CONSTRAINT part_a_value_check CHECK (val < 100);

-- Insert a row into partition A
INSERT INTO check_part_test VALUES (1, 50);

-- Attempt to move to partition B
-- Expected: successful
UPDATE check_part_test SET partid = 20, val = 900 WHERE partid = 1;

SELECT * FROM part_a;
SELECT * FROM part_b;

-- New scenario: CHECK constraint on partitionA and partitionB 
-- Clean slate
TRUNCATE check_part_test;

-- Add Constraint CHECK on partition B, remains on partition A
ALTER TABLE part_b ADD CONSTRAINT part_b_value_check CHECK (val < 200);

-- Insert a row into partition A
INSERT INTO check_part_test VALUES (1, 50);

-- Attempt to move to partition B where value fails constraint CHECK
-- Expected: ERROR on partition B
UPDATE check_part_test SET partid = 20, val = 900 WHERE partid = 1;

SELECT * FROM part_a;
SELECT * FROM part_b;

-- New scenario: CHECK constraint on partitionB (two rows) 
-- Clean slate
TRUNCATE check_part_test;

-- Remove any existing CHECK constraint from part_a
ALTER TABLE part_a DROP CONSTRAINT IF EXISTS part_a_value_check;

-- Insert two rows into partition A
INSERT INTO check_part_test VALUES (1, 50);
INSERT INTO check_part_test VALUES (2, 60);

-- Attempt multi-row update: one fails CHECK on partition B, one stays on partition A
-- Expected: entire statement fails, error due to partition B constraint
UPDATE check_part_test SET val = val + 800, partid = partid + 8 WHERE partid IN (1, 2);

SELECT * FROM part_a;
SELECT * FROM part_b;

-- New scenario: CHECK constraint on partitionA (two rows)
-- Clean slate
TRUNCATE check_part_test;

-- Remove any existing CHECK constraint from part_b
ALTER TABLE part_b DROP CONSTRAINT IF EXISTS part_b_value_check;
-- Constraint CHECK on partition A
ALTER TABLE part_a ADD CONSTRAINT part_a_value_check CHECK (val < 100);


-- Insert two rows into partition A
INSERT INTO check_part_test VALUES (1, 50);
INSERT INTO check_part_test VALUES (2, 60);

-- Attempt multi-row update: one moves to partition B, one stays on partition A
-- Expected: successful
UPDATE check_part_test SET val = val + 40, partid = partid + 8 WHERE partid IN (1, 2);

SELECT * FROM part_a;
SELECT * FROM part_b;


-- New scenario: CHECK constraint on partitionA and partitionB (two rows)
-- Clean slate
TRUNCATE check_part_test;

-- Constraint CHECK on partition B
ALTER TABLE part_b ADD CONSTRAINT part_b_value_check CHECK (val < 150);

-- Insert two rows into partition A
INSERT INTO check_part_test VALUES (1, 30);
INSERT INTO check_part_test VALUES (2, 90);

-- Attempt multi-row update: one fails CHECK on partition B, one stays on partition A
-- Expected: entire statement fails, error due to partition B constraint
UPDATE check_part_test SET val = val + 60, partid = partid + 8 WHERE partid IN (1, 2);

SELECT * FROM part_a;
SELECT * FROM part_b;