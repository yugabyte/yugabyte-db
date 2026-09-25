--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- under row-level security.
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

CREATE INDEX NONCONCURRENTLY bkt_tbl_expr_idx ON bkt_tbl ((yb_hash_code(r3, r2, r4, r5, r1) % 3) ASC, r1, r2, r3, r4, r5);
ANALYZE bkt_tbl;

CREATE ROLE regress_merge_scan_rls_user;
GRANT SELECT ON bkt_tbl TO regress_merge_scan_rls_user;
ALTER TABLE bkt_tbl ENABLE ROW LEVEL SECURITY;
-- The policy passes every row so that correct results are identical with and
-- without RLS applied.
CREATE POLICY bkt_tbl_rls_policy ON bkt_tbl
    FOR SELECT TO regress_merge_scan_rls_user USING (n >= 0);

SET ROLE regress_merge_scan_rls_user;

-- Derived SAOPs with no query clause: both access paths merge through SAOPs
-- derived from the secondary index expression and the generated pk bucket
-- column.  Derived SAOPs are exempt from the securely-promotable check, so
-- they form streams even for an RLS subject.
\set Q1 '/*+IndexScan(bkt_tbl bkt_tbl_pkey) Set(yb_max_merge_scan_streams 64) Set(yb_enable_derived_saops on)*/'
\set Q2 '/*+IndexScan(bkt_tbl bkt_tbl_expr_idx) Set(yb_max_merge_scan_streams 64) Set(yb_enable_derived_saops on)*/'
\set query ':P :Q SELECT r1, r2, r3, r4, r5 FROM bkt_tbl ORDER BY r1, r2, r3, r4, r5 LIMIT 5;'
\i :run_query
\unset Q2

-- A clause on the generated bucket column, without derivation: the clause
-- matches the leading pk column and is leakproof (bare int column, builtin
-- equality), so it remains securely promotable under RLS.  The promoted clause
-- supplies the pk streams.
\set Q1 '/*+IndexScan(bkt_tbl bkt_tbl_pkey) Set(yb_max_merge_scan_streams 64) Set(yb_enable_derived_saops off)*/'
\set query ':P :Q1 SELECT r1, r2, r3, r4, r5, bkt FROM bkt_tbl WHERE bkt IN (0, 1, 2) ORDER BY r1, r2, r3, r4, r5 LIMIT 5;'
\i :run_query

-- A clause on the index expression, without derivation: the clause matches the
-- secondary index expression, but neither yb_hash_code nor int4mod is
-- leakproof, so under RLS the clause is not securely promotable and must not
-- form merge streams.  The hint falls back to an ordinary sorted plan with the
-- clause as a filter.
\set Q1 '/*+IndexScan(bkt_tbl bkt_tbl_expr_idx) Set(yb_max_merge_scan_streams 64) Set(yb_enable_derived_saops off)*/'
\set query ':P :Q1 SELECT r1, r2, r3, r4, r5, yb_hash_code(r3, r2, r4, r5, r1) % 3 FROM bkt_tbl WHERE yb_hash_code(r3, r2, r4, r5, r1) % 3 IN (0, 1, 2) ORDER BY r1, r2, r3, r4, r5 LIMIT 5;'
\i :run_query

RESET ROLE;

-- The table owner is not subject to RLS: the same query and hint now promote
-- the clause, and merge scan engages.
\i :run_query

-- A policy that breaks an EquivalenceClass.  rls_ec_tbl is of no use to other
-- tests, so it is defined here rather than in yb.orig.merge_scan_setup.  d = t
-- uses the date = timestamp operator, which is not leakproof, so under RLS,
-- the planner cannot build d = 2020-01-02 from the query's t = 2020-01-02 and
-- keeps d = t and t = 2020-01-02 as they are.  The pathkeys walk still sees d
-- as a constant, but d has no index condition, so it must not be a stream key,
-- and the merge sorts on d as well.  The first row of each stream is a decoy
-- that fails d = t.
CREATE TABLE rls_ec_tbl (s int, d date, c int, t timestamp,
    PRIMARY KEY (s ASC, d ASC, c ASC));
INSERT INTO rls_ec_tbl
    SELECT i % 3, date '2020-01-01' + (i % 4), i,
        timestamp '2020-01-01' + (i % 4) * interval '1 day'
    FROM generate_series(1, 300) i;
INSERT INTO rls_ec_tbl VALUES
    (1, date '2020-01-02', 1000, timestamp '2020-01-05'),
    (2, date '2020-01-01', 1001, timestamp '2020-01-02');
ANALYZE rls_ec_tbl;
GRANT SELECT ON rls_ec_tbl TO regress_merge_scan_rls_user;
ALTER TABLE rls_ec_tbl ENABLE ROW LEVEL SECURITY;
CREATE POLICY rls_ec_tbl_policy ON rls_ec_tbl
    FOR SELECT TO regress_merge_scan_rls_user USING (d = t);

SET ROLE regress_merge_scan_rls_user;
-- Third hint turns sort off so that the merge scan is chosen.
\set Q1 ':off'
\set Q2 ':on'
\set Q3 '/*+Set(enable_sort off) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c, s, d FROM rls_ec_tbl WHERE s IN (1, 2) AND t = make_timestamp(2020, 1, 2, 0, 0, 0) ORDER BY c LIMIT 4;'
\i :run_query
-- With expression pushdown off, d = t runs after the merge.
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;
-- The same broken EquivalenceClass, with the query also holding d at
-- 2020-01-02.  date = date is leakproof, so that clause binds d as an index
-- condition, and d is a stream key.
\set query ':P :Q SELECT c, s, d FROM rls_ec_tbl WHERE s IN (1, 2) AND d = make_date(2020, 1, 2) AND t = make_timestamp(2020, 1, 2, 0, 0, 0) ORDER BY c LIMIT 4;'
\i :run_query
-- With expression pushdown off, d = t runs after the merge.  d is still bound
-- to one value by its index condition, so each stream stays sorted by c, and
-- the merge scan on s and d returns the rows in order.
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;
RESET ROLE;

-- A hash column in a broken EquivalenceClass.  rls_ec_hash_tbl has the rows and
-- policy of rls_ec_tbl, with (s, d) as its hash key.  pggate cannot merge
-- without a condition on every hash column, so d stays a stream key, and
-- without an index condition on d, the merge scan errors.  The error names s,
-- the first stream key column, because pggate binds no hash column when one
-- lacks a condition.  With the query also holding d at 2020-01-02, the scan
-- binds d and runs.  TODO(#34120): the first query should run without error.
CREATE TABLE rls_ec_hash_tbl (s int, d date, c int, t timestamp,
    PRIMARY KEY ((s, d) HASH, c ASC));
INSERT INTO rls_ec_hash_tbl SELECT * FROM rls_ec_tbl;
ANALYZE rls_ec_hash_tbl;
GRANT SELECT ON rls_ec_hash_tbl TO regress_merge_scan_rls_user;
ALTER TABLE rls_ec_hash_tbl ENABLE ROW LEVEL SECURITY;
CREATE POLICY rls_ec_hash_tbl_policy ON rls_ec_hash_tbl
    FOR SELECT TO regress_merge_scan_rls_user USING (d = t);

SET ROLE regress_merge_scan_rls_user;
\set query ':P :Q SELECT c, s, d FROM rls_ec_hash_tbl WHERE s IN (1, 2) AND t = make_timestamp(2020, 1, 2, 0, 0, 0) ORDER BY c LIMIT 4;'
\i :run_query
\set query ':P :Q SELECT c, s, d FROM rls_ec_hash_tbl WHERE s IN (1, 2) AND d = make_date(2020, 1, 2) AND t = make_timestamp(2020, 1, 2, 0, 0, 0) ORDER BY c LIMIT 4;'
\i :run_query
RESET ROLE;

-- A policy operator that cannot be pushed down.  rls_tz_tbl is rls_ec_tbl with
-- d timestamp and t timestamptz.  timestamp = timestamptz is not leakproof, so
-- under RLS it breaks the class like date = timestamp.  It is also stable, so
-- the policy's filter stays local with expression pushdown on.
SET TimeZone = 'UTC';
CREATE TABLE rls_tz_tbl (s int, d timestamp, c int, t timestamptz,
    PRIMARY KEY (s ASC, d ASC, c ASC));
INSERT INTO rls_tz_tbl SELECT s, d, c, t FROM rls_ec_tbl;
ANALYZE rls_tz_tbl;
GRANT SELECT ON rls_tz_tbl TO regress_merge_scan_rls_user;
ALTER TABLE rls_tz_tbl ENABLE ROW LEVEL SECURITY;
CREATE POLICY rls_tz_tbl_policy ON rls_tz_tbl
    FOR SELECT TO regress_merge_scan_rls_user USING (d = t);

SET ROLE regress_merge_scan_rls_user;
\set query ':P :Q SELECT c, s, d FROM rls_tz_tbl WHERE s IN (1, 2) AND t = timestamptz \'2020-01-02 00:00:00+00\' ORDER BY c LIMIT 4;'
\i :run_query
\unset Q3
RESET ROLE;
RESET TimeZone;

-- Cleanup
DROP TABLE rls_ec_tbl, rls_ec_hash_tbl, rls_tz_tbl;
DROP POLICY bkt_tbl_rls_policy ON bkt_tbl;
ALTER TABLE bkt_tbl DISABLE ROW LEVEL SECURITY;
REVOKE SELECT ON bkt_tbl FROM regress_merge_scan_rls_user;
DROP ROLE regress_merge_scan_rls_user;
DROP INDEX bkt_tbl_expr_idx;
