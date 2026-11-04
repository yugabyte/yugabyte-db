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
