CREATE DATABASE pctest colocation = true;
\c pctest

-- large table to ensure multiple parallel ranges
CREATE TABLE pcustomer (
    pc_id int primary key,
    pc_name text,
    pc_address text,
    pc_phone text,
    pc_email text,
    pc_acctbalance numeric(15,2)) WITH (colocation = true);
INSERT INTO pcustomer
SELECT i, -- pc_id
       'Customer #' || i::text, -- pc_name
       (1234 + i * 2)::text || ' Unknown Str., Some Random City, XX, ' || (567 * i % 89991 + 10001)::text, -- pc_address
       '(' || (i % 399 + 100)::text || ')' || ((i + 13) % 897 + 101)::text || '-' || ((i + 4321) % 8999 + 1000)::text, -- pc_phone
       'customer' || i::text || '@example.com', -- pc_email
       random() * 25000.0 - 10000.0
FROM generate_series(1, 500000) i;
ANALYZE pcustomer;

set yb_parallel_range_rows to 10000;
set yb_enable_base_scans_cost_model to true;

EXPLAIN (costs off)
SELECT pc_id, pc_address, pc_phone, pc_email FROM pcustomer WHERE pc_name = 'Customer #42';
SELECT pc_id, pc_address, pc_phone, pc_email FROM pcustomer WHERE pc_name = 'Customer #42';

-- parallel query in transaction and subtransaction
BEGIN;

UPDATE pcustomer SET pc_acctbalance = 0 WHERE pc_id >= 40 AND pc_id < 50;
EXPLAIN (costs off)
SELECT * FROM pcustomer WHERE pc_name LIKE 'Customer #4_';
SELECT * FROM pcustomer WHERE pc_name LIKE 'Customer #4_';

SAVEPOINT svp;
UPDATE pcustomer SET pc_acctbalance = 100 WHERE pc_id = 42;
EXPLAIN (costs off)
SELECT * FROM pcustomer WHERE pc_name LIKE 'Customer #4_';
SELECT * FROM pcustomer WHERE pc_name LIKE 'Customer #4_';

COMMIT;

DROP TABLE pcustomer;

