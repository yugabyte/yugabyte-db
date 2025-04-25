CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();

CREATE SCHEMA create_index;
SET search_path TO create_index;

-- Test temp table and index.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY, a int);
CREATE INDEX foo_idx_temp on temp_foo(a);

DROP INDEX foo_idx_temp;
DROP TABLE temp_foo;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;

-- Create base table.
CREATE TABLE foo(i int PRIMARY KEY, a int, b text, c int);

-- Create indexes.
CREATE INDEX foo_idx_simple ON foo(a);

CREATE UNIQUE INDEX foo_idx_unique ON foo(b);

CREATE INDEX foo_idx_filtered ON foo(c ASC, a) WHERE a > c;

-- Test that role is captured properly.
CREATE ROLE new_role SUPERUSER;
SET ROLE new_role;
CREATE INDEX foo_idx_include ON foo(lower(b)) INCLUDE (a) SPLIT INTO 2 TABLETS;
SET ROLE NONE;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- Now drop these indexes.
-- Drop two indexes by themselves.
DROP INDEX foo_idx_unique;
DROP INDEX foo_idx_filtered;

-- Drop base table and cascade deletion of other indexes.
DROP TABLE foo;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

select * from public.TEST_verify_replicated_ddls();
