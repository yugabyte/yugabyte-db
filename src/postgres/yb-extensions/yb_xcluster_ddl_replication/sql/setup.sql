-- Initial test setup.
CREATE EXTENSION yb_xcluster_ddl_replication;
ALTER DATABASE :DBNAME SET yb_xcluster_ddl_replication.replication_role = SOURCE;

-- Setup function, to be called at the top of each test file.
CREATE PROCEDURE TEST_reset()
  LANGUAGE SQL AS
$$
  DELETE FROM yb_xcluster_ddl_replication.ddl_queue;
  DELETE FROM yb_xcluster_ddl_replication.replicated_ddls;
$$;

-- Verify that both tables have the same keys.
CREATE OR REPLACE FUNCTION TEST_verify_replicated_ddls()
  RETURNS boolean
  LANGUAGE SQL AS
$$
  SELECT (count(1) = 0) FROM yb_xcluster_ddl_replication.ddl_queue a
  FULL OUTER JOIN yb_xcluster_ddl_replication.replicated_ddls b
  USING (ddl_end_time, query_id)
  WHERE a.yb_data IS NULL OR b.yb_data IS NULL;
$$;
