--
-- Test pg_stat backend functions.
--
-- Avoid outputting pid, dbid, catalog_version since those can vary between
-- runs.
--

-- Test pg_stat_activity view.  The three rows correspond to checkpointer,
-- this connection, and the java test.
SELECT datname, usename, state, query, backend_type,
       catalog_version IS NOT null AS has_catalog_snapshot
    FROM pg_stat_activity;

-- Test yb_pg_stat_get_backend_catalog_version.
SELECT beid, yb_pg_stat_get_backend_catalog_version(beid) IS NOT null
             AS has_catalog_snapshot
    FROM pg_stat_get_backend_idset() beid;

-- Test that yb_pg_stat_get_backend_catalog_version for this backend matches
-- yb_catalog_version.
SELECT beid, yb_catalog_version() - be_catalog_version AS catalog_version_diff
    FROM pg_stat_get_backend_idset() beid,
         yb_pg_stat_get_backend_catalog_version(beid) be_catalog_version
    WHERE be_catalog_version IS NOT null;

-- Test pg_stat_get_backend_dbid for backends with yugabyte database.
SELECT beid, datname
    FROM pg_stat_get_backend_idset() beid
    JOIN pg_database d
    ON pg_stat_get_backend_dbid(beid) = d.oid
    WHERE datname = current_database() ORDER BY beid;

-- Test pg_stat_get_backend_dbid for backends with postgres database after
-- switching current connection to postgres database.
\c postgres
SELECT beid, datname
    FROM pg_stat_get_backend_idset() beid
    JOIN pg_database d
    ON pg_stat_get_backend_dbid(beid) = d.oid
    WHERE datname = current_database();


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
