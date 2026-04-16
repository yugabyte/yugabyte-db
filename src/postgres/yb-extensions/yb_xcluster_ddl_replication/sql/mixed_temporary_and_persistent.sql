CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();


-- Test dropping mix of temp and regular tables.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
CREATE TABLE foo(i int PRIMARY KEY);
DROP TABLE temp_foo, foo; -- should fail
DROP TABLE foo, temp_foo; -- should fail
DROP TABLE temp_foo;
DROP TABLE foo;

SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
