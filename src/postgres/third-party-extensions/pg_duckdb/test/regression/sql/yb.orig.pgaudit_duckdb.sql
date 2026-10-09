-- YB: pgaudit + pg_duckdb interaction guard.
--
-- pg_duckdb runs a Postgres permission check on the source relations of COPY (<query>) TO (utility
-- path, CheckQueryPermissions). With pgaudit loaded, that check fires pgaudit's
-- ExecutorCheckPerms_hook at utility time -- before ExecutorStart builds the executor stack -- when
-- pgaudit's auditEventStack is NULL, which segfaults the backend without the guard. YB clears the hook around the
-- core ACL check (pgduckdb::pg::YbExecCheckRTPerms), so it must now run cleanly and leave the
-- backend alive.
--
-- Audit-record note: because the hook is cleared for this check, pgaudit currently emits NO audit
-- record for the source reads of this DuckDB-routed COPY (a known audit gap). Session
-- audit output also goes to the server log, not the client, so nothing pgaudit-related appears
-- below yet. TODO(#32512): once the durable pgaudit fix (a NULL-auditEventStack guard) lands and
-- the hook can stay installed, add `SET pgaudit.log_client = ON;` / `SET pgaudit.log_level =
-- 'notice';` here and capture the restored AUDIT lines in this expected output.
--
-- Requires pgaudit loaded; it is in YugabyteDB's default shared_preload_libraries (pg_wrapper adds
-- "pgaudit" unconditionally), so no test-specific preload setup is needed.
\set VERBOSITY terse

CREATE EXTENSION pgaudit;

-- Activate pgaudit read-path auditing so its ExecutorCheckPerms_hook is exercised.
SET pgaudit.log = 'read';

CREATE TABLE yb_pgaudit_t (id int PRIMARY KEY, val text);
INSERT INTO yb_pgaudit_t VALUES (1, 'a'), (2, 'b');

-- COPY (<query>) TO routed through DuckDB -> CheckQueryPermissions -> YbExecCheckRTPerms on the
-- source relation yb_pgaudit_t. Without the guard, this COPY segfaults with pgaudit loaded.
COPY (SELECT id, val FROM yb_pgaudit_t) TO '/tmp/yb_pgaudit_duckdb.parquet';

-- Sentinel: this segfault is synchronous, so a crash would already fail the diff at the COPY;
-- kept as a cheap safety net.
SELECT 'backend still alive' AS status;

DROP TABLE yb_pgaudit_t;
DROP EXTENSION pgaudit;
