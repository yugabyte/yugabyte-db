-- Lake statements inside transaction blocks under YB READ COMMITTED.
-- READ COMMITTED opens an internal subtransaction around every statement of a transaction block so
-- the statement can be retried; that subtransaction must not count as a user SAVEPOINT. Requires the
-- tserver flag yb_enable_read_committed_isolation=true (set by
-- TestPgRegressThirdPartyExtensionsPgDuckDb).
\getenv abs_srcdir PG_ABS_SRCDIR
\set parq :abs_srcdir '/data/iris.parquet'

CREATE TABLE yb_rc_t (id int PRIMARY KEY);

BEGIN ISOLATION LEVEL READ COMMITTED;
SHOW transaction_isolation;
-- Several lake statements in one block: the first one starts the DuckDB transaction, the later
-- ones each run in a fresh internal subtransaction.
SELECT count(*) AS rows_1 FROM read_parquet(:'parq');
SELECT count(*) AS rows_2 FROM read_parquet(:'parq');
CREATE TABLE yb_rc_ctas AS SELECT r['variety']::text AS variety FROM read_parquet(:'parq') AS r;
INSERT INTO yb_rc_t VALUES (1);
COPY (SELECT id FROM yb_rc_t) TO '/tmp/yb_rc_copy.parquet';
DECLARE c CURSOR FOR SELECT r['variety']::text AS variety FROM read_parquet(:'parq') AS r ORDER BY 1;
FETCH 1 FROM c;
CLOSE c;
COMMIT;
SELECT count(*) AS ctas_rows FROM yb_rc_ctas;
SELECT count(*) AS exported_rows FROM read_parquet('/tmp/yb_rc_copy.parquet');

-- must fail: a real SAVEPOINT before the first lake statement is still rejected.
BEGIN;
SAVEPOINT s1;
SELECT count(*) FROM read_parquet(:'parq');
ROLLBACK;

-- must fail: a SAVEPOINT after a lake statement is still rejected.
BEGIN;
SELECT count(*) AS rows_before_savepoint FROM read_parquet(:'parq');
SAVEPOINT s2;
ROLLBACK;

-- must fail: a PL/pgSQL EXCEPTION block is a real (anonymous) subtransaction, also outside BEGIN.
-- (The path goes in as a literal via format(): table-function arguments can't be parameters.)
SET yb_rc.parq = :'parq';
-- terse: the error CONTEXT would print the machine-specific file path.
\set VERBOSITY terse
DO $$
DECLARE n bigint;
BEGIN
  BEGIN
    EXECUTE format('SELECT count(*) FROM read_parquet(%L)', current_setting('yb_rc.parq')) INTO n;
  EXCEPTION WHEN division_by_zero THEN
    NULL;
  END;
END $$;
-- must fail: the same EXCEPTION block inside a READ COMMITTED block.
BEGIN ISOLATION LEVEL READ COMMITTED;
DO $$
DECLARE n bigint;
BEGIN
  BEGIN
    EXECUTE format('SELECT count(*) FROM read_parquet(%L)', current_setting('yb_rc.parq')) INTO n;
  EXCEPTION WHEN division_by_zero THEN
    NULL;
  END;
END $$;
ROLLBACK;
\set VERBOSITY default

-- Cleanup
DROP TABLE yb_rc_ctas;
DROP TABLE yb_rc_t;
