BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8106, 'yb_pg_stat_plans_insert', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, true,
  false, 'v', 's',
  10, 0, 16,
  '26 26 20 20 25 25 1184 1184 701 701', NULL, NULL,
  '{dbid, user_id, query_id, plan_id, hint_text, plan_text, first_used, last_used, total_time, est_total_cost}', NULL, NULL,
  'yb_pg_stat_plans_insert', NULL, NULL,
  '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8106, 1255, 0, 'Insert an entry into the QPM table')
    ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8106, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8107, 'yb_pg_stat_plans_get_all_entries', 11, 10,
  12, 1, 1000,
  0, '-', 'f',
  false, false, false,
  true, 'v', 's',
  0, 0, 2249,
  '', NULL, NULL,
  NULL, NULL, NULL,
  'yb_pg_stat_plans_get_all_entries', NULL, NULL,
  NULL)
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8107, 1255, 0, 'Retrieve all QPM entries')
    ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8108, 'yb_pg_stat_plans_reset', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, false,
  false, 'v', 's',
  4, 0, 20,
  '26 26 20 20', NULL, NULL,
  '{dbid, user_id, query_id, plan_id}', NULL, NULL,
  'yb_pg_stat_plans_reset', NULL, NULL,
  '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8108, 1255, 0, 'Remove QPM entries')
    ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8108, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8109, 'yb_pg_stat_plans_total_time', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, false,
  false, 'v', 's',
  0, 0, 701,
  '', NULL, NULL,
  NULL, NULL, NULL,
  'yb_pg_stat_plans_total_time', NULL, NULL,
  NULL)
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8109, 1255, 0, 'Total elapsed time spent in QPM code')
    ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8110, 'yb_pg_stat_plans_total_calls', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, false,
  false, 'v', 's',
  0, 0, 20,
  '', NULL, NULL,
  NULL, NULL, NULL,
  'yb_pg_stat_plans_total_calls', NULL, NULL,
  NULL)
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8110, 1255, 0, 'Total number of calls to QPM code')
    ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8111, 'yb_pg_stat_plans_read_file', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, false,
  false, 'v', 's',
  0, 0, 23,
  '', NULL, NULL,
  NULL, NULL, NULL,
  'yb_pg_stat_plans_read_file', NULL, NULL,
  '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8111, 1255, 0, 'Read the QPM dump file')
    ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8111, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner,
  prolang, procost, prorows,
  provariadic, prosupport, prokind,
  prosecdef, proleakproof, proisstrict,
  proretset, provolatile, proparallel,
  pronargs, pronargdefaults, prorettype,
  proargtypes, proallargtypes, proargmodes,
  proargnames, proargdefaults, protrftypes,
  prosrc, probin, proconfig,
  proacl
) VALUES
  (8112, 'yb_pg_stat_plans_write_file', 11, 10,
  12, 1, 0,
  0, '-', 'f',
  false, false, false,
  false, 'v', 's',
  0, 0, 23,
  '', NULL, NULL,
  NULL, NULL, NULL,
  'yb_pg_stat_plans_write_file', NULL, NULL,
  '{postgres=X/postgres,yb_db_admin=X/postgres}')
ON CONFLICT DO NOTHING;

-- Insert the record for pg_description.
INSERT INTO pg_catalog.pg_description (
		objoid, classoid, objsubid, description
	) VALUES
		(8112, 1255, 0, 'Write the QPM dump file')
    ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8112, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

COMMIT;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_views
    WHERE viewname = 'yb_pg_stat_plans'
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_pg_stat_plans
    WITH (use_initdb_acl = true)
    AS
      SELECT *
       FROM yb_pg_stat_plans_get_all_entries() AS stat_plans(dbid oid, userid oid,
	   										queryid BIGINT, planid BIGINT,
	   										first_used TIMESTAMPTZ, last_used TIMESTAMPTZ,
											hints TEXT, calls BIGINT,
											avg_exec_time DOUBLE PRECISION,
											max_exec_time DOUBLE PRECISION, max_exec_time_params TEXT,
											avg_est_cost DOUBLE PRECISION, plan TEXT);
  END IF;
END $$;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_views
    WHERE viewname = 'yb_pg_stat_plans_insights'
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_pg_stat_plans_insights
    WITH (use_initdb_acl = true)
    AS
      WITH cte AS (SELECT dbid, userid, queryid, planid, first_used, last_used, hints, avg_exec_time, avg_est_cost,
	             min(avg_exec_time) OVER (PARTITION BY dbid, userid, queryid) min_avg_exec_time,
				 min(avg_est_cost) OVER (PARTITION BY dbid, userid, queryid) min_avg_est_cost FROM yb_pg_stat_plans)
		SELECT dbid, userid, queryid, planid, first_used, last_used, hints, avg_exec_time, avg_est_cost,
	       min_avg_exec_time, min_avg_est_cost, CASE WHEN (avg_exec_time = min_avg_exec_time AND
		   min_avg_est_cost != avg_est_cost) OR (avg_exec_time != min_avg_exec_time AND
		   min_avg_est_cost = avg_est_cost) THEN 'Yes' ELSE 'No' END AS plan_require_evaluation,
		   CASE WHEN avg_exec_time = min_avg_exec_time THEN 'Yes' ELSE 'No' END AS plan_min_exec_time
		FROM cte ORDER BY queryid, planid, last_used;
  END IF;
END $$;
