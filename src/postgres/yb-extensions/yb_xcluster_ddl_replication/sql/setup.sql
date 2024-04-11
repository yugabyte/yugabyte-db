-- Initial test setup.
CREATE EXTENSION yb_xcluster_ddl_replication;
ALTER DATABASE :DBNAME SET yb_xcluster_ddl_replication.replication_role = BIDIRECTIONAL;

-- Setup function, to be called at the top of each test file.
CREATE PROCEDURE TEST_reset()
  LANGUAGE SQL AS
$$
  SET yb_xcluster_ddl_replication.ddl_queue_primary_key_start_time = 0;
  SET yb_xcluster_ddl_replication.ddl_queue_primary_key_query_id = 1;
  DELETE FROM yb_xcluster_ddl_replication.ddl_queue;
  DELETE FROM yb_xcluster_ddl_replication.replicated_ddls;
$$;

-- The consumer extension expects for ddl_queue_primary_key_start_time and
-- ddl_queue_primary_key_query_id to be set by the xCluster DDL handler.
-- For this test, we will instead just use a trigger to increment these values.
CREATE FUNCTION TEST_set_target_variables()
  RETURNS event_trigger
  LANGUAGE plpgsql AS
$$
DECLARE
  start_time bigint;
  replication_role text;
  enable_manual bool;
BEGIN
  replication_role := current_setting('yb_xcluster_ddl_replication.replication_role', true);
  enable_manual := current_setting('yb_xcluster_ddl_replication.enable_manual_ddl_replication', true);
  IF enable_manual = TRUE OR
     NOT (replication_role = 'TARGET' OR replication_role = 'BIDIRECTIONAL') THEN
    RETURN;
  END IF;

  BEGIN
    start_time := current_setting(
        'yb_xcluster_ddl_replication.ddl_queue_primary_key_start_time');
    start_time := start_time + 1;
    PERFORM set_config(
        'yb_xcluster_ddl_replication.ddl_queue_primary_key_start_time', start_time::text, false);
  EXCEPTION WHEN undefined_object THEN
    -- Should only happen during tear down when the variable is not set.
    RETURN;
  END;
END;
$$;

CREATE EVENT TRIGGER TEST_update_consumer_test_variables
  ON ddl_command_start
  EXECUTE FUNCTION TEST_set_target_variables();
