-- Enable CBO
SET yb_enable_optimizer_statistics = true;
SET yb_enable_base_scans_cost_model = true;

SET yb_enable_derived_equalities = true;

-- Point lookups on single relation
CREATE TABLE orders (
    order_id int,
    user_id int,
    created_at timestamptz,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(user_id::text || order_id::text) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, user_id ASC, order_id ASC)
);

-- Secondary index with expression (not generated column)
CREATE INDEX orders_created_idx ON orders((yb_hash_code(created_at) % 3) ASC, created_at ASC);

INSERT INTO orders VALUES
    (1, 100, '2024-01-01'::timestamptz),
    (2, 100, '2024-01-02'::timestamptz);

ANALYZE orders;

-- Multi-column bucket (bucket depends on user_id AND order_id)
EXPLAIN (COSTS OFF) SELECT * FROM orders WHERE user_id = 100 AND order_id = 1;
SELECT * FROM orders WHERE user_id = 100 AND order_id = 1;

-- Expression index
EXPLAIN (COSTS OFF) SELECT /*+ IndexScan(orders orders_created_idx) */ * FROM orders WHERE created_at = '2024-01-02'::timestamptz;
SELECT * FROM orders WHERE created_at = '2024-01-02'::timestamptz;

-- Stable expression
EXPLAIN (COSTS OFF) SELECT /*+ IndexScan(orders orders_created_idx) */ * FROM orders WHERE created_at = date_trunc('day', '2024-01-02 15:30:12.789'::timestamptz);
SELECT * FROM orders WHERE created_at = date_trunc('day', '2024-01-02 15:30:12.789'::timestamptz);

-- Parameterized query
PREPARE order_query AS SELECT * FROM orders WHERE user_id = $1 AND order_id = $2;
EXPLAIN (COSTS OFF) EXECUTE order_query(100, 2);
EXECUTE order_query(100, 2);

DROP TABLE orders;

-- Tests for joins
CREATE TABLE users (
    id int,
    email text
);

INSERT INTO users VALUES (50, 'user50@test.com'), (60, 'user60@test.com'), (70, 'user70@test.com'), (80, 'user80@test.com');

-- Table 1: No bucket (baseline)
CREATE TABLE orders_no_bucket (
    order_id int,
    user_id int
);

CREATE INDEX orders_no_bucket_user_id_idx ON orders_no_bucket(user_id ASC);

INSERT INTO orders_no_bucket VALUES (1, 50), (2, 50), (3, 60), (4, 70);

-- Table 2: With generated column in PRIMARY KEY
CREATE TABLE orders_gen (
    order_id int,
    user_id int,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(user_id) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, user_id ASC, order_id ASC)
);

INSERT INTO orders_gen VALUES (1, 50), (2, 50), (3, 60), (4, 70);

-- Table 3: With expression index
CREATE TABLE orders_expr (
    order_id int,
    user_id int
);

CREATE INDEX orders_expr_idx ON orders_expr((yb_hash_code(user_id) % 3) ASC, user_id ASC);

INSERT INTO orders_expr VALUES (1, 50), (2, 50), (3, 60), (4, 70);

ANALYZE users, orders_no_bucket, orders_gen, orders_expr;

SET yb_prefer_bnl = false;
SET yb_enable_batchednl = false;

-- Baseline NL
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_no_bucket_user_id_idx) */ * FROM users u JOIN orders_no_bucket o ON o.user_id = u.id;

-- NL with generated column (should show bucket condition)
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id;

-- NL with expression index (should show bucket condition)
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_expr_idx) */ * FROM users u JOIN orders_expr o ON o.user_id = u.id;

SET yb_prefer_bnl = true;
SET yb_enable_batchednl = true;

-- Baseline BNL
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_no_bucket_user_id_idx) */ * FROM users u JOIN orders_no_bucket o ON o.user_id = u.id;

-- BNL with generated column (should show bucket condition)
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id;

-- TODO: BNL is not supported when the index clause is on an expression column
-- This will pick NL
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_expr_idx) */ * FROM users u JOIN orders_expr o ON o.user_id = u.id;

-- With WHERE clause filter
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50;
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id WHERE u.id IN (50, 60, 70);

-- Condition in ON clause
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id AND u.id = 50;
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u JOIN orders_gen o ON o.user_id = u.id AND u.id IN (50, 60, 70);

-- Equality conditions in outer joins

-- Left join with WHERE on non-nullable side
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 80;
SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 80 ORDER BY u.id, o.order_id;

-- Left join with WHERE on nullable side
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 50;
SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 50 ORDER BY u.id, o.order_id;

-- Left join with WHERE in ON clause
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id AND o.user_id = 50;
SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id AND o.user_id = 50 ORDER BY u.id, o.order_id;

-- Left join with expression index
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_expr_idx) */ * FROM users u LEFT JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50;
SELECT * FROM users u LEFT JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;

-- Full join with WHERE on either side
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50;
SELECT * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;

EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 60;
SELECT * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 60 ORDER BY u.id, o.order_id;

-- Full join with WHERE on both sides
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o) */ * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50 AND o.user_id = 50;

-- Full join with expression index
EXPLAIN (COSTS OFF) SELECT /*+ NestLoop(u o) IndexScan(o orders_expr_idx) */ * FROM users u FULL JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50;
SELECT * FROM users u FULL JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;

-- Multi-way joins
CREATE TABLE t1 (x int);
CREATE TABLE t2 (x int, bucket_id int GENERATED ALWAYS AS (yb_hash_code(x) % 5) STORED,
                 PRIMARY KEY (bucket_id, x));
CREATE TABLE t3 (x int);

INSERT INTO t1 SELECT generate_series(1, 1000);
INSERT INTO t2 SELECT generate_series(1, 1000);
INSERT INTO t3 SELECT generate_series(1, 1000);

ANALYZE t1, t2, t3;
SET enable_hashjoin = false;
SET enable_mergejoin = false;

EXPLAIN (COSTS OFF) SELECT * FROM t1 JOIN t2 ON t2.x = t1.x JOIN t3 ON t3.x = t1.x;
SET join_collapse_limit = 1;
EXPLAIN (COSTS OFF) SELECT * FROM t3 JOIN t2 ON t2.x = t3.x JOIN t1 ON t1.x = t3.x;
RESET join_collapse_limit;

-- Cleanup
DROP TABLE users;
DROP TABLE orders_no_bucket;
DROP TABLE orders_gen;
DROP TABLE orders_expr;
DROP TABLE t1, t2, t3;
