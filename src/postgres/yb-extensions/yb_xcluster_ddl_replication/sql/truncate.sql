CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();

CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
CREATE TABLE foo(i int PRIMARY KEY);
TRUNCATE foo, temp_foo;  -- should fail
TRUNCATE foo;
TRUNCATE temp_foo;

SELECT yb_data FROM public.TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
