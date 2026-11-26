-- Verify yb_db_admin can alter rows in tables it does not own
CREATE TABLE foo(a INT);
INSERT INTO foo VALUES (0);
INSERT INTO foo VALUES (1);
INSERT INTO foo VALUES (2);
SET SESSION AUTHORIZATION yb_db_admin;
SELECT * from foo ORDER BY a;
DELETE FROM foo WHERE a = 2;
SELECT * from foo ORDER BY a;
UPDATE foo SET a = 5 WHERE a = 0;
SELECT * from foo ORDER BY a;
TRUNCATE foo;
SELECT * from foo ORDER BY a;
-- Check yb_db_admin cannot alter sys tables
UPDATE pg_shdepend SET dbid=1 WHERE dbid=0;
DELETE from pg_shdepend WHERE dbid=0;
TRUNCATE pg_shdepend;
RESET SESSION AUTHORIZATION;
-- Revoking a role must invalidate cached plans that applied its policy
-- (CVE-2026-14666)
CREATE TABLE rls_plancache (k INT PRIMARY KEY);
INSERT INTO rls_plancache VALUES (1), (2);
ALTER TABLE rls_plancache ENABLE ROW LEVEL SECURITY;
CREATE ROLE rls_plancache_reader;
CREATE ROLE rls_plancache_user;
GRANT rls_plancache_reader TO rls_plancache_user;
CREATE POLICY p ON rls_plancache FOR SELECT TO rls_plancache_reader
    USING (true);
GRANT SELECT ON rls_plancache TO rls_plancache_user;
SET ROLE rls_plancache_user;
PREPARE rls_plancache_q AS SELECT k FROM rls_plancache ORDER BY k;
EXECUTE rls_plancache_q;
RESET ROLE;
REVOKE rls_plancache_reader FROM rls_plancache_user;
SET ROLE rls_plancache_user;
EXECUTE rls_plancache_q;
RESET ROLE;
-- So must a change to the role's attributes
ALTER ROLE rls_plancache_user BYPASSRLS;
SET ROLE rls_plancache_user;
EXECUTE rls_plancache_q;
RESET ROLE;
DEALLOCATE rls_plancache_q;
DROP TABLE rls_plancache;
DROP ROLE rls_plancache_user, rls_plancache_reader;
-- A clause pushed to DocDB runs before the clauses kept on the PostgreSQL
-- side, so a non-leakproof user clause must not be pushed past a policy qual
-- that is not pushable (DB-25002)
CREATE ROLE rls_pushdown_user;
CREATE FUNCTION rls_pushdown_is_owner(o text) RETURNS boolean
    LANGUAGE plpgsql STABLE AS $$ BEGIN RETURN o = current_user; END $$;
CREATE TABLE rls_pushdown (k int PRIMARY KEY, owner text, v int, w int);
CREATE INDEX NONCONCURRENTLY rls_pushdown_w_v ON rls_pushdown (w, v);
INSERT INTO rls_pushdown VALUES (1, 'admin', 0, 1), (2, 'rls_pushdown_user', 5, 1);
ALTER TABLE rls_pushdown ENABLE ROW LEVEL SECURITY;
GRANT SELECT ON rls_pushdown TO rls_pushdown_user;
CREATE POLICY p ON rls_pushdown FOR SELECT
    USING (rls_pushdown_is_owner(owner));
SET ROLE rls_pushdown_user;
-- The policy is not pushable, so the user clause stays local as well
/*+ SeqScan(rls_pushdown) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE 10 / v > 0;
/*+ SeqScan(rls_pushdown) */
SELECT * FROM rls_pushdown WHERE 10 / v > 0;
-- A leakproof user clause may still be pushed
/*+ SeqScan(rls_pushdown) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE v = 5;
/*+ SeqScan(rls_pushdown) */
SELECT * FROM rls_pushdown WHERE v = 5;
RESET ROLE;
-- The policy is pushable: the user clause follows it to DocDB, policy first
DROP POLICY p ON rls_pushdown;
CREATE POLICY p ON rls_pushdown FOR SELECT USING (owner = current_user);
SET ROLE rls_pushdown_user;
/*+ SeqScan(rls_pushdown) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE 10 / v > 0;
/*+ SeqScan(rls_pushdown) */
SELECT * FROM rls_pushdown WHERE 10 / v > 0;
-- The policy is pushed to the table, so the user clause must not be pushed
-- to the secondary index, which is evaluated first
/*+ IndexScan(rls_pushdown rls_pushdown_w_v) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
/*+ IndexScan(rls_pushdown rls_pushdown_w_v) */
SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
-- A bitmap scan evaluates the index clauses first as well
SET yb_enable_bitmapscan = true;
/*+ BitmapScan(rls_pushdown rls_pushdown_w_v) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
/*+ BitmapScan(rls_pushdown rls_pushdown_w_v) */
SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
RESET ROLE;
-- The policy is not pushable: the user clause skips both DocDB stages
DROP POLICY p ON rls_pushdown;
CREATE POLICY p ON rls_pushdown FOR SELECT
    USING (rls_pushdown_is_owner(owner));
SET ROLE rls_pushdown_user;
/*+ IndexScan(rls_pushdown rls_pushdown_w_v) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
/*+ IndexScan(rls_pushdown rls_pushdown_w_v) */
SELECT * FROM rls_pushdown WHERE w = 1 AND 10 / v > 0;
RESET ROLE;
DROP TABLE rls_pushdown;
-- A bitmap scan that exceeds work_mem falls back to a full scan with every
-- pushable clause; that list must be in security order as well
CREATE TABLE rls_pushdown_big (k int PRIMARY KEY, owner text, v int);
CREATE INDEX NONCONCURRENTLY rls_pushdown_big_v ON rls_pushdown_big (v ASC);
INSERT INTO rls_pushdown_big
    SELECT i, CASE WHEN i % 2 = 0 THEN 'rls_pushdown_user' ELSE 'admin' END, i
    FROM generate_series(1, 8000) i;
ALTER TABLE rls_pushdown_big ENABLE ROW LEVEL SECURITY;
GRANT SELECT ON rls_pushdown_big TO rls_pushdown_user;
CREATE POLICY p ON rls_pushdown_big FOR SELECT USING (owner = current_user);
SET ROLE rls_pushdown_user;
SET work_mem = '64kB';
/*+ BitmapScan(rls_pushdown_big rls_pushdown_big_v) */
EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT k FROM rls_pushdown_big WHERE v > 0 AND 1 / (v - 7) >= 0 AND k < 10
    ORDER BY k;
/*+ BitmapScan(rls_pushdown_big rls_pushdown_big_v) */
SELECT k FROM rls_pushdown_big WHERE v > 0 AND 1 / (v - 7) >= 0 AND k < 10
    ORDER BY k;
RESET work_mem;
RESET yb_enable_bitmapscan;
RESET ROLE;
DROP TABLE rls_pushdown_big;
-- Policies split across the groups: a non-leakproof user clause goes behind
-- every policy, a leakproof one keeps its pushdown
CREATE FUNCTION rls_pushdown_is_pos(i int) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE AS $$ BEGIN RETURN i > 0; END $$;
CREATE TABLE rls_pushdown_mix (k int PRIMARY KEY, owner text, v int, w int);
CREATE INDEX NONCONCURRENTLY rls_pushdown_mix_w_v ON rls_pushdown_mix (w, v);
INSERT INTO rls_pushdown_mix
    VALUES (1, 'admin', 0, 1), (2, 'rls_pushdown_user', 5, 1);
ALTER TABLE rls_pushdown_mix ENABLE ROW LEVEL SECURITY;
GRANT SELECT ON rls_pushdown_mix TO rls_pushdown_user;
CREATE POLICY p ON rls_pushdown_mix FOR SELECT USING (owner = current_user);
CREATE POLICY r ON rls_pushdown_mix AS RESTRICTIVE FOR SELECT
    USING (rls_pushdown_is_owner(owner));
SET ROLE rls_pushdown_user;
/*+ SeqScan(rls_pushdown_mix) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown_mix
    WHERE 10 / v > 0 AND v = 5 AND rls_pushdown_is_pos(v);
/*+ SeqScan(rls_pushdown_mix) */
SELECT * FROM rls_pushdown_mix
    WHERE 10 / v > 0 AND v = 5 AND rls_pushdown_is_pos(v);
/*+ IndexScan(rls_pushdown_mix rls_pushdown_mix_w_v) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown_mix
    WHERE w = 1 AND 10 / v > 0 AND v = 5 AND rls_pushdown_is_pos(v);
/*+ IndexScan(rls_pushdown_mix rls_pushdown_mix_w_v) */
SELECT * FROM rls_pushdown_mix
    WHERE w = 1 AND 10 / v > 0 AND v = 5 AND rls_pushdown_is_pos(v);
RESET ROLE;
-- Three security levels: policy (0), security barrier view (1), user (2).
-- The view's non-leakproof clause waits for the policy; the user's leakproof
-- clause is pushed at any level
CREATE VIEW rls_pushdown_sv WITH (security_barrier) AS
    SELECT * FROM rls_pushdown_mix WHERE 10 / v > 0;
ALTER VIEW rls_pushdown_sv OWNER TO rls_pushdown_user;
DROP POLICY p ON rls_pushdown_mix;
DROP POLICY r ON rls_pushdown_mix;
CREATE POLICY p ON rls_pushdown_mix FOR SELECT
    USING (rls_pushdown_is_owner(owner));
SET ROLE rls_pushdown_user;
/*+ SeqScan(rls_pushdown_mix) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown_sv WHERE v = 5;
/*+ SeqScan(rls_pushdown_mix) */
SELECT * FROM rls_pushdown_sv WHERE v = 5;
/*+ IndexScan(rls_pushdown_mix rls_pushdown_mix_w_v) */
EXPLAIN (COSTS OFF) SELECT * FROM rls_pushdown_sv WHERE w = 1 AND v = 5;
/*+ IndexScan(rls_pushdown_mix rls_pushdown_mix_w_v) */
SELECT * FROM rls_pushdown_sv WHERE w = 1 AND v = 5;
RESET ROLE;
DROP VIEW rls_pushdown_sv;
DROP TABLE rls_pushdown_mix;
DROP FUNCTION rls_pushdown_is_pos(int);
DROP FUNCTION rls_pushdown_is_owner(text);
DROP ROLE rls_pushdown_user;
