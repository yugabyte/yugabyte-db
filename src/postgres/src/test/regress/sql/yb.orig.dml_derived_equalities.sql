\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (COSTS OFF)'

-- Enable CBO
SET yb_enable_optimizer_statistics = true;
SET yb_enable_base_scans_cost_model = true;

SET yb_enable_derived_equalities = true;
-- Don't want derived SAOPs to mask coverage of derived equalities.
SET yb_enable_derived_saops = false;

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
\set query ':P SELECT * FROM orders WHERE user_id = 100 AND order_id = 1;'
\i :iter_P2

\set hint '/*+ IndexScan(orders orders_created_idx) */'

-- Expression index
SELECT $$
:P :hint SELECT * FROM orders WHERE created_at = '2024-01-02'::timestamptz;
$$ AS query \gset
\i :iter_P2

-- Stable expression
SELECT $$
:P :hint SELECT * FROM orders WHERE created_at = date_trunc('day', '2024-01-02 15:30:12.789'::timestamptz);
$$ AS query \gset
\i :iter_P2

-- Parameterized query
PREPARE order_query AS SELECT * FROM orders WHERE user_id = $1 AND order_id = $2;
\set query ':P EXECUTE order_query(100, 2);'
\i :iter_P2

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

-- Table 2: With generated column in PRIMARY KEY
CREATE TABLE orders_gen (
    order_id int,
    user_id int,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(user_id) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, user_id ASC, order_id ASC)
);

-- Table 3: With expression index
CREATE TABLE orders_expr (
    order_id int,
    user_id int
);
CREATE INDEX orders_expr_idx ON orders_expr((yb_hash_code(user_id) % 3) ASC, user_id ASC);

\set R1 'orders_no_bucket'
\set R2 'orders_gen'
\set R3 'orders_expr'
\set query 'INSERT INTO :R VALUES (1, 50), (2, 50), (3, 60), (4, 70);'
\i :iter_R3

ANALYZE users, orders_no_bucket, orders_gen, orders_expr;

\set Q1 '/*+ Set(yb_prefer_bnl off) NestLoop(u o) IndexScan(o) */'
\set Q2 '/*+ YbBatchedNL(u o) IndexScan(o) */'
\set Qnext :iter_R3
\set query ':explain :Q SELECT * FROM users u JOIN :R o ON o.user_id = u.id;'

-- TODO: BNL is not supported when the index clause is on an expression column
\i :iter_Q2

\set hint '/*+ NestLoop(u o) IndexScan(o) */'
\set R1 'u.id = 50'
\set R2 'u.id IN (50, 60, 70)'

-- With WHERE clause filter
\set query ':explain :hint SELECT * FROM users u JOIN orders_gen o ON o.user_id = u.id WHERE :R;'
\i :iter_R2

-- Condition in ON clause
\set query ':explain :hint SELECT * FROM users u JOIN orders_gen o ON o.user_id = u.id AND :R;'
\i :iter_R2

-- Equality conditions in outer joins

-- Left join with WHERE on non-nullable side
\set query ':P :hint SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 80 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Left join with WHERE on nullable side
\set query ':P :hint SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Left join with WHERE in ON clause
\set query ':P :hint SELECT * FROM users u LEFT JOIN orders_gen o ON o.user_id = u.id AND o.user_id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Left join with expression index
\set query ':P :hint SELECT * FROM users u LEFT JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Full join with WHERE on either side
\set query ':P :hint SELECT * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

\set query ':P :hint SELECT * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 60 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Full join with WHERE on both sides
\set query ':P :hint SELECT * FROM users u FULL JOIN orders_gen o ON o.user_id = u.id WHERE u.id = 50 AND o.user_id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Full join with expression index
\set query ':P :hint SELECT * FROM users u FULL JOIN orders_expr o ON o.user_id = u.id WHERE u.id = 50 ORDER BY u.id, o.order_id;'
\i :iter_P2

-- Multi-way joins
CREATE TABLE t1 (x int);
CREATE TABLE t2 (x int, bucket_id int GENERATED ALWAYS AS (yb_hash_code(x) % 5) STORED,
                 PRIMARY KEY (bucket_id, x));
CREATE TABLE t3 (x int);

\set R1 't1'
\set R2 't2'
\set R3 't3'
\set query 'INSERT INTO :R SELECT generate_series(1, 1000);'
\i :iter_R3

ANALYZE t1, t2, t3;
SET enable_hashjoin = false;
SET enable_mergejoin = false;

:explain SELECT * FROM t1 JOIN t2 ON t2.x = t1.x JOIN t3 ON t3.x = t1.x;
:explain SELECT * FROM t3 JOIN t2 ON t2.x = t3.x JOIN t1 ON t1.x = t3.x;

-- Cleanup
DROP TABLE users;
DROP TABLE orders_no_bucket;
DROP TABLE orders_gen;
DROP TABLE orders_expr;
DROP TABLE t1, t2, t3;

-- Ensure yb_derive_equal_cond does not crash when an index relcache entry
-- is invalidated mid-planning (after get_relation_info but before path
-- generation reads it).  (see GH#31313)
SET yb_test_invalidate_relcache_in_planner = true;
CREATE TABLE t_invalidate (a int, b int);
CREATE INDEX ON t_invalidate ((a + b));
INSERT INTO t_invalidate VALUES (1, 4), (2, 3);
SELECT * FROM t_invalidate WHERE a + b = 5 ORDER BY a;
RESET yb_test_invalidate_relcache_in_planner;
DROP TABLE t_invalidate;
