-- Initial test setup.
CREATE EXTENSION yb_xcluster_ddl_replication;

-- workaround for lack of CREATE ROLE IF NOT EXISTS
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'testuser') THEN
    CREATE ROLE testuser LOGIN;
  END IF;
END
$$;

-- Setup function, to be called at the top of each test file.
CREATE PROCEDURE TEST_reset()
  LANGUAGE SQL AS
$$
  SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'source';
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

-- Select from ddl_queue, but redact any oids / colocation ids.
CREATE OR REPLACE FUNCTION TEST_filtered_ddl_queue()
  RETURNS SETOF yb_xcluster_ddl_replication.ddl_queue
  LANGUAGE plpgsql AS
$$
DECLARE
  rec RECORD;
  new_array JSONB;
  element JSONB;
  k TEXT;
  v TEXT;
BEGIN
  for rec in
        select * from yb_xcluster_ddl_replication.ddl_queue
    loop
        if rec.yb_data->'new_rel_map' is null then
            return next rec;
            continue;
        end if;
        new_array := '[]'::jsonb;
        for element in
            select * from jsonb_array_elements(rec.yb_data->'new_rel_map')
        loop
            for k, v in
                select * from jsonb_each_text(element)
            loop
                if k = 'relfile_oid' or k = 'colocation_id' then
                    element := jsonb_set(element, ('{' || k || '}')::text[], '"***"');
                end if;
            end loop;
            new_array := new_array || element;
        end loop;
        rec.yb_data := jsonb_set(rec.yb_data, '{new_rel_map}', new_array);
        return next rec;
    end loop;
END
$$;
