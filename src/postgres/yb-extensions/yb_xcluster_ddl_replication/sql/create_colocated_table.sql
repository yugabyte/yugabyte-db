\c colocation_test
CALL TEST_reset();

-- Verify that temporary objects are allowed but not captured.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);

-- Verify that colocated tables are allowed.
CREATE TABLE coloc_foo(i int PRIMARY KEY);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;

-- Verify that non-colocated table is captured.
CREATE TABLE non_coloc_foo(i int PRIMARY KEY) WITH (COLOCATION = false);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

select * from TEST_verify_replicated_ddls();
