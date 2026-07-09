BEGIN;
  CREATE EXTENSION IF NOT EXISTS postgres_fdw SCHEMA pg_catalog;

  CREATE SERVER IF NOT EXISTS yb_global_views_server
      FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'federatedYugabyteDB');

  -- Creates <view>_with_server_uuid and the gv$<view> foreign table over it
  CREATE OR REPLACE FUNCTION pg_catalog.yb_create_global_view(
    schema_name text, view_name text)
  RETURNS void
  LANGUAGE plpgsql
  AS $$
DECLARE
    source_rel   text := quote_ident('pg_catalog') || '.' || quote_ident(view_name);
    wrapper_name text := view_name || '_with_server_uuid';
    wrapper_rel  text := quote_ident(schema_name) || '.' || quote_ident(wrapper_name);
    foreign_rel  text := quote_ident(schema_name) || '.' || quote_ident('gv$' || view_name);
    source_oid   oid  := to_regclass(source_rel);
    col_defs     text;
BEGIN
    -- Fail with a clear message if the source view is absent.
    IF source_oid IS NULL THEN
        RAISE EXCEPTION 'yb_create_global_view: relation % does not exist', source_rel;
    END IF;
    -- Only plain views are supported
    IF (SELECT relkind FROM pg_class WHERE oid = source_oid) <> 'v' THEN
        RAISE EXCEPTION 'yb_create_global_view: relation % is not a view', source_rel;
    END IF;
    -- Views cannot have dropped columns; error out rather than silently skip one.
    IF EXISTS (SELECT 1 FROM pg_attribute
               WHERE attrelid = source_rel::regclass AND attnum > 0 AND attisdropped) THEN
        RAISE EXCEPTION 'yb_create_global_view: view % has dropped columns', source_rel;
    END IF;
    -- Create the wrapper view. It is pg_catalog-qualified; the initdb env var and
    -- IsYsqlUpgrade let it past the system-view guard during initdb and upgrade.
    EXECUTE format(
        'CREATE OR REPLACE VIEW %s AS '
        'SELECT yb_get_local_tserver_uuid() AS server_uuid, * FROM %s',
        wrapper_rel, source_rel);
    -- The remote query runs as the non-superuser yb_global_views_user (a member
    -- of pg_read_all_stats), so grant it read access to the wrapper view.
    EXECUTE format('GRANT SELECT ON %s TO pg_read_all_stats', wrapper_rel);
    SELECT string_agg(
               quote_ident(attname) || ' ' || format_type(atttypid, atttypmod),
               ', ' ORDER BY attnum)
      INTO col_defs
      FROM pg_attribute
      WHERE attrelid = wrapper_rel::regclass
        AND attnum > 0;
    EXECUTE format(
        'CREATE FOREIGN TABLE IF NOT EXISTS %s (%s) '
        'SERVER yb_global_views_server '
        'OPTIONS (schema_name %L, table_name %L)',
        foreign_rel, col_defs, schema_name, wrapper_name);
    -- gv$ wrappers expose pg_read_all_stats-level data, so restrict to that role
    EXECUTE format('GRANT SELECT ON %s TO pg_read_all_stats', foreign_rel);
END;
$$;

  -- Restrict to privileged users
  REVOKE EXECUTE ON FUNCTION pg_catalog.yb_create_global_view(text, text) FROM PUBLIC;

  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'yb_active_session_history');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_statements');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_activity');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'yb_terminated_queries');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_progress_copy');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'yb_pg_stat_plans');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'yb_pg_stat_plans_insights');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_all_tables');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_progress_create_index');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_database');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_user_tables');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_user_indexes');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_user_functions');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_progress_analyze');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_all_indexes');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_replication');
  SELECT pg_catalog.yb_create_global_view('pg_catalog', 'pg_stat_replication_slots');
COMMIT;

-- Record the grants above as initial privileges so pg_dump omits them
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  INSERT INTO pg_catalog.pg_init_privs (objoid, classoid, objsubid, initprivs, privtype)
      SELECT c.oid, 'pg_catalog.pg_class'::regclass, 0, c.relacl, 'i'
      FROM pg_catalog.pg_class c
      WHERE c.relnamespace = 'pg_catalog'::regnamespace
        AND c.relacl IS NOT NULL
        AND (c.relname LIKE 'gv$%' OR c.relname LIKE '%\_with\_server\_uuid')
  ON CONFLICT DO NOTHING;

  -- Likewise record the REVOKE on yb_create_global_view so pg_dump omits it
  INSERT INTO pg_catalog.pg_init_privs (objoid, classoid, objsubid, initprivs, privtype)
      SELECT p.oid, 'pg_catalog.pg_proc'::regclass, 0, p.proacl, 'i'
      FROM pg_catalog.pg_proc p
      WHERE p.pronamespace = 'pg_catalog'::regnamespace
        AND p.proname = 'yb_create_global_view'
        AND p.proacl IS NOT NULL
  ON CONFLICT DO NOTHING;
COMMIT;
