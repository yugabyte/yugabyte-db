--
-- Test pg_stat backend functions.
--
-- Avoid outputting pid, dbid, catalog_version since those can vary between
-- runs.
--

-- Test pg_stat_activity view.  The four rows correspond to checkpointer,
-- this connection, the java test, and the ASH collector.
SELECT datname, usename, state, query, backend_type,
       catalog_version IS NOT null AS has_catalog_snapshot
    FROM pg_stat_activity ORDER BY usename;

-- Test yb_pg_stat_get_backend_catalog_version.
SELECT beid,
       backend_type,
       yb_pg_stat_get_backend_catalog_version(beid) IS NOT NULL AS has_catalog_snapshot
FROM pg_stat_get_backend_idset() beid
JOIN pg_stat_activity s ON pg_stat_get_backend_pid(beid) = s.pid;

-- Test that yb_pg_stat_get_backend_catalog_version for this backend matches
-- yb_catalog_version.
SELECT beid,
       backend_type,
       yb_catalog_version() - be_catalog_version AS catalog_version_diff
FROM pg_stat_get_backend_idset() AS beid
JOIN pg_stat_activity s ON pg_stat_get_backend_pid(beid) = s.pid
CROSS JOIN yb_pg_stat_get_backend_catalog_version(beid) AS be_catalog_version
WHERE be_catalog_version IS NOT NULL;

-- Test pg_stat_get_backend_dbid for backends with yugabyte database.
SELECT beid,
       backend_type,
       d.datname AS database_name
FROM pg_stat_get_backend_idset() AS beid
JOIN pg_database d ON pg_stat_get_backend_dbid(beid) = d.oid
JOIN pg_stat_activity s ON pg_stat_get_backend_pid(beid) = s.pid
WHERE d.datname = current_database()
ORDER BY beid;

-- Test pg_stat_get_backend_dbid for backends with postgres database after
-- switching current connection to postgres database.
\c postgres
SELECT beid,
       backend_type,
       d.datname AS database_name
FROM pg_stat_get_backend_idset() AS beid
JOIN pg_database d ON pg_stat_get_backend_dbid(beid) = d.oid
JOIN pg_stat_activity s ON pg_stat_get_backend_pid(beid) = s.pid
WHERE d.datname = current_database()
ORDER BY beid;


-- Test yb_pg_stat_get_backend_catalog_version follows yb_catalog_version
-- during DDLs.
CREATE TABLE actual_expected (actual int, expected int);
ALTER TABLE actual_expected ADD UNIQUE (actual);
ALTER TABLE actual_expected ADD UNIQUE (expected);
INSERT INTO actual_expected
    SELECT catalog_version, yb_catalog_version()
    FROM pg_stat_get_backend_idset() beid,
         yb_pg_stat_get_backend_catalog_version(beid) catalog_version,
         pg_stat_get_backend_pid(beid) pid
    WHERE pid = pg_backend_pid();
CREATE TABLE tmp (i int);
DROP TABLE tmp;
INSERT INTO actual_expected
    SELECT catalog_version, yb_catalog_version()
    FROM pg_stat_get_backend_idset() beid,
         yb_pg_stat_get_backend_catalog_version(beid) catalog_version,
         pg_stat_get_backend_pid(beid) pid
    WHERE pid = pg_backend_pid();
SELECT actual = expected AS check FROM actual_expected;
