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

INSERT INTO orders SELECT i, 100, '2024-01-01'::timestamptz + ((i-1)::text||' days')::interval
    FROM generate_series(1, 100) i;

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

-- OR clauses (bitmap scan paths)
-- A bitmap scan builds a separate index path for each arm of an OR.  So the
-- derived equality must be inferred independently inside each arm; then an arm
-- that constrains user_id can still drive an index whose leading column is the
-- generated/expression bucket computed from user_id.
-- TODO(#31354 or #31355): Recheck Cond should be removed.
SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;

-- Use a custom Rnext that builds BitmapScan(:R) hint per table.
\set Pnext :iter_R3
\set Rnext :abs_srcdir '/yb_commands/bitmap_hint_iter_R.sql'
\set R1 'orders_no_bucket'
\set R2 'orders_gen'
\set R3 'orders_expr'

-- Three-way OR
\set query ':P :hint SELECT * FROM :R WHERE user_id = 50 OR user_id = 60 OR user_id = 70 ORDER BY user_id, order_id;'
\i :iter_P2

-- OR with AND
\set query ':P :hint SELECT * FROM :R WHERE (user_id = 50 AND order_id = 1) OR (order_id = 3 AND user_id = 60) ORDER BY user_id, order_id;'
\i :iter_P2

-- Restore defaults.
\set Pnext :iter_query
\set Rnext :iter_query

-- Join with an OR on a column that is also the join key.  The derivation must
-- still apply within each arm so the bitmap scan on orders_gen uses the bucket.
\set hint '/*+ NestLoop(u o) BitmapScan(o) */'
SELECT $$
:P :hint SELECT * FROM users u JOIN orders_gen o ON o.user_id = u.id WHERE o.user_id = 50 OR o.user_id = 60 ORDER BY u.id, o.order_id;
$$ AS query \gset
\i :iter_P2

-- A NULL constant does not pin the column to a value, so no bucket is derived
-- from a "user_id = NULL" arm (the arm is also a constant the planner drops).
\set hint '/*+ BitmapScan(orders_gen) */'
\set query ':P :hint SELECT * FROM orders_gen WHERE user_id = 50 OR user_id = NULL ORDER BY user_id, order_id;'
\i :iter_P2

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
\set query ':P :hint SELECT * FROM transitive WHERE y = x AND ((x = 5 AND z = y) OR (x = 6 AND z = 6)) ORDER BY id;'
\i :iter_P2
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
-- index (_expr).  :R iterates the two tables; BitmapScan(:R) hints whichever
-- index applies.

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
\set R1 'guard_type_gen'
\set R2 'guard_type_expr'
\set query 'INSERT INTO :R(c) VALUES (5), (6), (7);'
\i :iter_R2
\set Pnext :iter_R2
\set Rnext :abs_srcdir '/yb_commands/bitmap_hint_iter_R.sql'
\set query ':P :hint SELECT c FROM :R WHERE c = 6 OR c = 5::bigint ORDER BY c;'
\i :iter_P2
\set Pnext :iter_query
\set Rnext :iter_query
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
\set R1 'guard_coerce_gen'
\set R2 'guard_coerce_expr'
SELECT $$
INSERT INTO :R(c) VALUES ('a'), ('b'), ('c');
$$ AS query \gset
\i :iter_R2
\set Pnext :iter_R2
\set Rnext :abs_srcdir '/yb_commands/bitmap_hint_iter_R.sql'
SELECT $$
:P :hint SELECT c FROM :R WHERE c = 'a'::text OR c = 'b' ORDER BY c;
$$ AS query \gset
\i :iter_P2
\set Pnext :iter_query
\set Rnext :iter_query
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
\set R1 'guard_op_gen'
\set R2 'guard_op_expr'
SELECT $$
INSERT INTO :R(id, x, y) VALUES (1, 1, '((0,0),(4,1))'), (2, 2, '((0,0),(1,4))');
$$ AS query \gset
\i :iter_R2
\set Pnext :iter_R2
\set Rnext :abs_srcdir '/yb_commands/bitmap_hint_iter_R.sql'
SELECT $$
:P :hint SELECT id, x FROM :R WHERE (x = 1 AND y = '((0,0),(2,2))'::box) OR (x = 2 AND y = '((1,1),(3,3))'::box) ORDER BY id;
$$ AS query \gset
\i :iter_P2
\set Pnext :iter_query
\set Rnext :iter_query
DROP TABLE guard_op_gen, guard_op_expr;

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

-- Constant index expressions must not produce trivial derived equalities
-- like "Index Cond: ((1) = 1)" (GH#31195).
CREATE TABLE t_const_expr (a int);
CREATE INDEX NONCONCURRENTLY t_const_expr_idx ON t_const_expr ((1)) INCLUDE (a);
INSERT INTO t_const_expr VALUES (10), (20);
ANALYZE t_const_expr;
\set hint '/*+ IndexScan(t_const_expr t_const_expr_idx) */'
\set query ':P :hint SELECT * FROM t_const_expr ORDER BY a;'
\i :iter_P2

-- Mixed index where only some expression columns are constant: derived
-- equalities should still fire for the non-constant expression column.
CREATE TABLE t_mixed_expr (a int, b int);
CREATE INDEX NONCONCURRENTLY t_mixed_expr_idx
    ON t_mixed_expr ((b + 1), (1)) INCLUDE (a);
INSERT INTO t_mixed_expr VALUES (1, 9), (2, 4);
ANALYZE t_mixed_expr;
\set hint '/*+ IndexScan(t_mixed_expr t_mixed_expr_idx) */'
\set query ':P :hint SELECT * FROM t_mixed_expr WHERE b = 4 ORDER BY a;'
\i :iter_P2

DROP TABLE t_const_expr;
DROP TABLE t_mixed_expr;

-- Derived equalities must not push a clause whose substituted RHS
-- evaluates (or could evaluate) to NULL.  (see GH#31971)
CREATE TABLE demo_a (id int PRIMARY KEY, x int);
INSERT INTO demo_a SELECT g, g % 7 FROM generate_series(1, 70) g;
CREATE INDEX demo_a_nullif_idx ON demo_a (nullif(x, 0));
ANALYZE demo_a;
CREATE TABLE demo_b (id int PRIMARY KEY, x int);
INSERT INTO demo_b SELECT g, g % 7 FROM generate_series(1, 70) g;
CREATE INDEX demo_b_nullif_idx ON demo_b (nullif(x, 0));
ANALYZE demo_b;

\set hint '/*+ IndexScan(demo_a demo_a_nullif_idx) */'

-- Single-rel, NULL-yielding const (no derived clause emitted)
\set query ':P :hint SELECT count(*) FROM demo_a WHERE x = 0;'
\i :iter_P2

-- Single-rel, non-NULL const (derived clause emitted)
\set query ':P :hint SELECT count(*) FROM demo_a WHERE x = 3;'
\i :iter_P2

\set hint '/*+ NestLoop(a b) IndexScan(b demo_b_nullif_idx) */'

-- Join with no constant predicate (no derived clause emitted)
\set query ':P :hint SELECT count(*) FROM demo_a a JOIN demo_b b ON a.x = b.x;'
\i :iter_P2

-- Join + WHERE on a join column = const (derived clause emitted)
\set query ':P :hint SELECT count(*) FROM demo_a a JOIN demo_b b ON a.x = b.x WHERE a.x = 5;'
\i :iter_P2

DROP TABLE demo_a, demo_b;

-- Fold-error guard: an index expression that would error during constant
-- folding (e.g. division by zero in (100 / 0)) must not propagate the
-- error out of planning.  The PG_TRY in yb_safely_fold_substituted
-- swallows the error and treats it as "can't derive, skip."
CREATE TABLE demo_err (id int PRIMARY KEY, x int);
INSERT INTO demo_err VALUES (1, 5), (2, 10);
CREATE INDEX demo_err_div_idx ON demo_err ((100 / x));
ANALYZE demo_err;
-- WHERE x = 0 has no matching rows; without the PG_TRY the planner errors
-- on (100 / 0) while folding the substituted side.
SELECT count(*) FROM demo_err WHERE x = 0;
DROP TABLE demo_err;

-- OR-arm variant: the bitmap-OR path's clause-based derivation must apply
-- the same NULL guard.  An arm like arr = '{}' would otherwise derive
-- array_length(arr, 1) = array_length('{}', 1), which folds to NULL and
-- silently drops the empty-array rows from that arm of the bitmap.
SET yb_enable_bitmapscan = true;
SET enable_bitmapscan = true;
CREATE TABLE demo_or (
    id int PRIMARY KEY,
    arr varchar[],
    bucket int GENERATED ALWAYS AS (array_length(arr, 1)) STORED
);
INSERT INTO demo_or (id, arr)
    SELECT g,
           CASE g % 3 WHEN 0 THEN '{}'::varchar[]
                      WHEN 1 THEN ARRAY['a']
                      ELSE ARRAY['a','b']
           END
    FROM generate_series(1, 30) g;
CREATE INDEX demo_or_bucket_idx ON demo_or (bucket);
ANALYZE demo_or;
-- id = 1 matches one row (arr = ARRAY['a']); arr = '{}' matches 10 rows; no
-- overlap.  Without the OR-arm guard the second arm derives bucket = NULL,
-- returns zero rows, and the count drops to 1.
\set hint '/*+ BitmapScan(demo_or) */'
\set query ':P :hint SELECT count(*) FROM demo_or WHERE id = 1 OR arr = ''{}'';'
\i :iter_P2
DROP TABLE demo_or;
RESET yb_enable_bitmapscan;
RESET enable_bitmapscan;
