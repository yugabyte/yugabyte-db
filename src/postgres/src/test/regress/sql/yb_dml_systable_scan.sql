--
-- KEY Pushdown Processing.
-- This file tests key-pushdown for system table scan.
--
-- Different from UserTable, system tables and its indexes are centralized in one tablet. To take
-- advantage of this fact, systable-scan queries the data using an INDEX key in one operation.
-- Normally it'd take two operations, one to select ROWID and another to select actual data.
--
-- Test forward scan.
EXPLAIN (COSTS OFF) SELECT classid, objid, objsubid, refclassid, refobjid, deptype FROM pg_depend
		WHERE deptype != 'p' AND deptype != 'e' AND deptype != 'i'
		ORDER BY classid, objid, objsubid
		LIMIT 2;

-- We cannot run the following SELECT on test because different platforms have different system
-- catalog data.
--
-- SELECT classid, objid, objsubid, refclassid, refobjid, deptype FROM pg_depend
--		WHERE deptype != 'p' AND deptype != 'e' AND deptype != 'i'
--		ORDER BY classid, objid, objsubid
--		LIMIT 2;

-- Test reverse scan.
EXPLAIN (COSTS OFF) SELECT classid, objid, objsubid, refclassid, refobjid, deptype FROM pg_depend
		WHERE deptype != 'p' AND deptype != 'e' AND deptype != 'i'
		ORDER BY classid DESC, objid DESC, objsubid DESC
		LIMIT 2;

-- We cannot run the following SELECT on test because different platforms have different system
-- catalog data.
--
-- SELECT classid, objid, objsubid, refclassid, refobjid, deptype FROM pg_depend
--		WHERE deptype != 'p' AND deptype != 'e' AND deptype != 'i'
--		ORDER BY classid DESC, objid DESC, objsubid DESC
--		LIMIT 2;

--
-- Test complex systable scans.
--

-- Existing db oid (template1).
SELECT * FROM pg_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 1);
SELECT * FROM pg_database WHERE datname IN (SELECT datname FROM pg_database WHERE oid = 1);

-- Invalid (non-existing) db.
SELECT * FROM pg_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 0);
SELECT * FROM pg_database WHERE datname IN (SELECT datname FROM pg_database WHERE oid = 0);

-- This is a query done by the pg_admin dashboard, testing compatiblity here.

-- Existing db oid (template1).
SELECT 'session_stats' AS chart_name, row_to_json(t) AS chart_data
FROM (SELECT
   (SELECT count(*) FROM pg_stat_activity WHERE datname = (SELECT datname FROM pg_database WHERE oid = 1)) AS "Total",
   (SELECT count(*) FROM pg_stat_activity WHERE state = 'active' AND datname = (SELECT datname FROM pg_database WHERE oid = 1))  AS "Active",
   (SELECT count(*) FROM pg_stat_activity WHERE state = 'idle' AND datname = (SELECT datname FROM pg_database WHERE oid = 1))  AS "Idle"
) t
UNION ALL
SELECT 'tps_stats' AS chart_name, row_to_json(t) AS chart_data
FROM (SELECT
   (SELECT sum(xact_commit) + sum(xact_rollback) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 1)) AS "Transactions",
   (SELECT sum(xact_commit) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 1)) AS "Commits",
   (SELECT sum(xact_rollback) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 1)) AS "Rollbacks"
) t;

-- Invalid (non-existing) db.
SELECT 'session_stats' AS chart_name, row_to_json(t) AS chart_data
FROM (SELECT
   (SELECT count(*) FROM pg_stat_activity WHERE datname = (SELECT datname FROM pg_database WHERE oid = 0)) AS "Total",
   (SELECT count(*) FROM pg_stat_activity WHERE state = 'active' AND datname = (SELECT datname FROM pg_database WHERE oid = 0))  AS "Active",
   (SELECT count(*) FROM pg_stat_activity WHERE state = 'idle' AND datname = (SELECT datname FROM pg_database WHERE oid = 0))  AS "Idle"
) t
UNION ALL
SELECT 'tps_stats' AS chart_name, row_to_json(t) AS chart_data
FROM (SELECT
   (SELECT sum(xact_commit) + sum(xact_rollback) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 0)) AS "Transactions",
   (SELECT sum(xact_commit) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 0)) AS "Commits",
   (SELECT sum(xact_rollback) FROM pg_stat_database WHERE datname = (SELECT datname FROM pg_database WHERE oid = 0)) AS "Rollbacks"
) t;

-- Test NULL returned by function.

-- Mark the function as stable to ensure pushdown.
CREATE OR REPLACE FUNCTION test_null_pushdown()
RETURNS Name AS $$
BEGIN
return null;
END;
$$ LANGUAGE plpgsql STABLE;

-- Expect pushdown in all cases.
EXPLAIN SELECT * FROM pg_database WHERE datname = test_null_pushdown();
EXPLAIN SELECT * FROM pg_database WHERE datname IN (test_null_pushdown());
EXPLAIN SELECT * FROM pg_database WHERE datname IN ('template1', test_null_pushdown(), 'template0');

-- Test execution.
SELECT * FROM pg_database WHERE datname = test_null_pushdown();
SELECT * FROM pg_database WHERE datname IN (test_null_pushdown());
-- Test null mixed with valid (existing) options.
SELECT * FROM pg_database WHERE datname IN ('template1', test_null_pushdown(), 'template0');
-- Test null(s) mixed with invalid (existing) options.
SELECT * FROM pg_database WHERE datname IN ('non_existing_db1', test_null_pushdown(), 'non_existing_db2', test_null_pushdown());

