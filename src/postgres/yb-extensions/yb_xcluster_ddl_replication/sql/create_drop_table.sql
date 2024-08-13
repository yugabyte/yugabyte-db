CALL TEST_reset();

-- Verify that temporary objects are not captured.
SET yb_xcluster_ddl_replication.replication_role = SOURCE;
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
DROP TABLE temp_foo;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SET yb_xcluster_ddl_replication.replication_role = BIDIRECTIONAL;

-- Verify that regular tables are captured.
CREATE TABLE foo(i int PRIMARY KEY);

-- Check with manual replication flags enabled, ddl string is captured with flag.
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
CREATE TABLE manual_foo(i int PRIMARY KEY);
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

-- Verify that extra info is captured.
CREATE TABLE extra_foo(i int PRIMARY KEY) WITH (COLOCATION = false) SPLIT INTO 1 TABLETS;

-- Verify that info for unique constraint indexes are also captured.
CREATE TABLE unique_foo(i int PRIMARY KEY, u text UNIQUE);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SELECT * FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY start_time;

-- Now test dropping these tables.
DROP TABLE foo;

-- Check with manual replication flags enabled, ddl string is captured with flag.
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
DROP TABLE manual_foo;
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

DROP TABLE extra_foo;
DROP TABLE unique_foo;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SELECT * FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY start_time;

-- Test mix of temp and regular tables.
SET yb_xcluster_ddl_replication.replication_role = SOURCE;
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
SET yb_xcluster_ddl_replication.replication_role = BIDIRECTIONAL;
CREATE TABLE foo(i int PRIMARY KEY);
DROP TABLE temp_foo, foo; -- should fail
DROP TABLE foo, temp_foo; -- should fail
DROP TABLE temp_foo;
DROP TABLE foo;
