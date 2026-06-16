\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/data/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (COSTS OFF)'

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

INSERT INTO orders SELECT i, 100, '2024-01-01'::timestamptz + ((i-1)::text||' days')::interval
    FROM generate_series(1, 100) i;

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

-- OR clauses (bitmap scan paths)
-- A bitmap scan builds a separate index path for each arm of an OR.  So the
-- derived equality must be inferred independently inside each arm; then an arm
-- that constrains user_id can still drive an index whose leading column is the
-- generated/expression bucket computed from user_id.
-- TODO(#31354 or #31355): Recheck Cond should be removed.
SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

-- Three-way OR
\set query '/*+ BitmapScan(orders_no_bucket) */ SELECT * FROM orders_no_bucket WHERE user_id = 50 OR user_id = 60 OR user_id = 70 ORDER BY user_id, order_id'
:explain1run1
\set query '/*+ BitmapScan(orders_gen) */ SELECT * FROM orders_gen WHERE user_id = 50 OR user_id = 60 OR user_id = 70 ORDER BY user_id, order_id'
:explain1run1
\set query '/*+ BitmapScan(orders_expr) */ SELECT * FROM orders_expr WHERE user_id = 50 OR user_id = 60 OR user_id = 70 ORDER BY user_id, order_id'
:explain1run1

-- OR with AND
\set query '/*+ BitmapScan(orders_no_bucket) */ SELECT * FROM orders_no_bucket WHERE (user_id = 50 AND order_id = 1) OR (order_id = 3 AND user_id = 60) ORDER BY user_id, order_id'
:explain1run1
\set query '/*+ BitmapScan(orders_gen) */ SELECT * FROM orders_gen WHERE (user_id = 50 AND order_id = 1) OR (order_id = 3 AND user_id = 60) ORDER BY user_id, order_id'
:explain1run1
\set query '/*+ BitmapScan(orders_expr) */ SELECT * FROM orders_expr WHERE (user_id = 50 AND order_id = 1) OR (order_id = 3 AND user_id = 60) ORDER BY user_id, order_id'
:explain1run1

-- Join with an OR on a column that is also the join key.  The derivation must
-- still apply within each arm so the bitmap scan on orders_gen uses the bucket.
\set hint '/*+ NestLoop(u o) BitmapScan(o) */'
SELECT $$
:hint SELECT * FROM users u JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 50 OR o.user_id = 60 ORDER BY u.id, o.order_id
$$ AS query \gset
:explain1run1

-- A NULL constant does not pin the column to a value, so no bucket is derived
-- from a "user_id = NULL" arm (the arm is also a constant the planner drops).
\set hint '/*+ BitmapScan(orders_gen) */'
\set query ':hint SELECT * FROM orders_gen WHERE user_id = 50 OR user_id = NULL ORDER BY user_id, order_id'
:explain1run1

-- TODO(#31672): within an OR arm an equality is only followed one step.
-- bucket_id is computed from z, so deriving it needs a constant value for z.
-- In arm 1, (x = 5 AND z = y), the only path to a constant is the chain z = y,
-- then y = x (a condition outside the OR), then x = 5 -- and that chain is not
-- followed, so arm 1 does not derive and falls back to a filter.  Arm 2 gives
-- z = 6 directly, so it derives.
CREATE TABLE transitive (
    id int,
    x int,
    y int,
    z int,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(z) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, x ASC)
);
INSERT INTO transitive(id, x, y, z) VALUES (1, 5, 5, 5), (2, 6, 6, 2);
\set hint '/*+ BitmapScan(transitive transitive_pkey) */'
\set query ':hint SELECT * FROM transitive WHERE y = x AND ((x = 5 AND z = y) OR (x = 6 AND z = 6)) ORDER BY id'
:explain1run1
DROP TABLE transitive;

-- Guards on when an OR-arm equality may be derived.
--
-- Deriving a bucket_id from an arm means substituting the constant from a
-- "column = constant" condition into the bucket expression.  That is only
-- valid when the condition truly pins the column to that one value: the
-- constant must be type-compatible with the column, and the operator must be a
-- real (btree) equality, not merely one the planner treats like equality for
-- selectivity.  The next three cases check both requirements.
--
-- Each case runs against both forms the derivation handles: the bucket as a
-- generated column in the primary key (_gen) and as an equivalent expression
-- index (_expr).

-- Type guard.  The bucket hashes c, an int.  "c = 5::bigint" uses the
-- cross-type int4/int8 "=" -- a real equality, but bigint is not
-- binary-coercible to int, so substituting the bigint constant into a hash of
-- an int would feed a differently-typed value and compute the wrong bucket.
-- So this arm must NOT derive the bucket; the same-type arm "c = 6" still does.
-- Both rows (5 and 6) are returned.
CREATE TABLE guard_type_gen (
    c int,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(c) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, c ASC)
);
CREATE TABLE guard_type_expr (c int);
CREATE INDEX NONCONCURRENTLY ON guard_type_expr ((yb_hash_code(c) % 5) ASC, c ASC);
INSERT INTO guard_type_gen(c) VALUES (5), (6), (7);
INSERT INTO guard_type_expr(c) VALUES (5), (6), (7);
\set query '/*+ BitmapScan(guard_type_gen) */ SELECT c FROM guard_type_gen WHERE c = 6 OR c = 5::bigint ORDER BY c'
:explain1run1
\set query '/*+ BitmapScan(guard_type_expr) */ SELECT c FROM guard_type_expr WHERE c = 6 OR c = 5::bigint ORDER BY c'
:explain1run1
DROP TABLE guard_type_gen, guard_type_expr;

-- Positive control.  Here c is varchar and one arm compares it to a text
-- constant.  text is binary-coercible to varchar (same representation), so
-- substituting the constant is safe and the derivation SHOULD still happen --
-- confirming the type requirement above does not over-reject compatible types.
CREATE TABLE guard_coerce_gen (
    c varchar,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(c) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, c ASC)
);
CREATE TABLE guard_coerce_expr (c varchar);
CREATE INDEX NONCONCURRENTLY ON guard_coerce_expr ((yb_hash_code(c) % 5) ASC, c ASC);
INSERT INTO guard_coerce_gen(c) VALUES ('a'), ('b'), ('c');
INSERT INTO guard_coerce_expr(c) VALUES ('a'), ('b'), ('c');
SELECT $$
/*+ BitmapScan(guard_coerce_gen) */ SELECT c FROM guard_coerce_gen WHERE c = 'a'::text OR c = 'b' ORDER BY c
$$ AS query \gset
:explain1run1
SELECT $$
/*+ BitmapScan(guard_coerce_expr) */ SELECT c FROM guard_coerce_expr WHERE c = 'a'::text OR c = 'b' ORDER BY c
$$ AS query \gset
:explain1run1
DROP TABLE guard_coerce_gen, guard_coerce_expr;

-- Operator guard.  The operator must be a real equality, not just one the
-- planner treats like equality for selectivity (an eqsel operator).  box's "="
-- is exactly that trap: it compares AREA (and box has no btree equality
-- opclass), so two different boxes of equal area count as equal.  Thus
-- "y = '((0,0),(2,2))'" (area 4) matches every area-4 box, not one specific
-- box.  The bucket hashes y, so deriving a single bucket from that constant
-- would wrongly drop the other equal-area rows.  The planner must skip the
-- derivation: the bucket stays out of the Index Cond and both equal-area rows
-- come back (matched by the recheck, not the index).
--
-- Test construction notes:
-- - each arm also constrains x (a plain indexed column) so the arm is a
--   candidate for an index path at all; otherwise the derivation step is never
--   reached;
-- - the arms use distinct box constants, because a condition common to every
--   arm is factored out of the OR before per-arm derivation runs (the same
--   one-step limitation as the transitive case above, #31672), so a shared
--   "y = <box>" would not be exercised here.
CREATE TABLE guard_op_gen (
    id int,
    x int,
    y box,
    bucket_id int GENERATED ALWAYS AS (yb_hash_code(y::text) % 5) STORED,
    PRIMARY KEY (bucket_id ASC, x ASC, id ASC)
);
CREATE TABLE guard_op_expr (id int, x int, y box);
CREATE INDEX NONCONCURRENTLY ON guard_op_expr ((yb_hash_code(y::text) % 5) ASC, x ASC, id ASC);
INSERT INTO guard_op_gen(id, x, y) VALUES (1, 1, '((0,0),(4,1))'), (2, 2, '((0,0),(1,4))');
INSERT INTO guard_op_expr(id, x, y) VALUES (1, 1, '((0,0),(4,1))'), (2, 2, '((0,0),(1,4))');
SELECT $$
/*+ BitmapScan(guard_op_gen) */ SELECT id, x FROM guard_op_gen WHERE (x = 1 AND y = '((0,0),(2,2))'::box) OR (x = 2 AND y = '((1,1),(3,3))'::box) ORDER BY id
$$ AS query \gset
:explain1run1
SELECT $$
/*+ BitmapScan(guard_op_expr) */ SELECT id, x FROM guard_op_expr WHERE (x = 1 AND y = '((0,0),(2,2))'::box) OR (x = 2 AND y = '((1,1),(3,3))'::box) ORDER BY id
$$ AS query \gset
:explain1run1
DROP TABLE guard_op_gen, guard_op_expr;

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
