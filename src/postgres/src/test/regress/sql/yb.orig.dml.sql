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
