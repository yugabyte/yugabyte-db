CALL TEST_reset();

-- Verify that temporary objects are not captured.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
DROP TABLE temp_foo;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;

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

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- Test tables partitioned by their primary key or a column.
CREATE TABLE foo_partitioned_by_pkey(id int, PRIMARY KEY (id)) PARTITION BY RANGE (id);
CREATE TABLE foo_partitioned_by_col(id int) PARTITION BY RANGE (id);

-- Now test dropping these tables.
DROP TABLE foo;

-- Check with manual replication flags enabled, ddl string is captured with flag.
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
DROP TABLE manual_foo;
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

DROP TABLE extra_foo;
DROP TABLE unique_foo;
DROP TABLE foo_partitioned_by_pkey;
DROP TABLE foo_partitioned_by_col;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- Test mix of temp and regular tables.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
CREATE TABLE foo(i int PRIMARY KEY);
DROP TABLE temp_foo, foo; -- should fail
DROP TABLE foo, temp_foo; -- should fail
DROP TABLE temp_foo;
DROP TABLE foo;

select * from TEST_verify_replicated_ddls();
