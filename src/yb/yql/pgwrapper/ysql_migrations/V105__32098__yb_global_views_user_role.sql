SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- Role used by global-view remote queries (PgRemoteExec). Non-superuser, no
-- password; only the internal yb-tserver-key path authenticates as it. It is a
-- member of pg_read_all_stats so it can read the stats views the feature needs.
INSERT INTO pg_catalog.pg_authid (
  oid, rolname, rolsuper, rolinherit, rolcreaterole, rolcreatedb, rolcanlogin,
  rolreplication, rolbypassrls, rolconnlimit, rolpassword, rolvaliduntil
) VALUES
  (8117, 'yb_global_views_user', false, true, false, false, true, false, false,
   -1, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Grant membership in pg_read_all_stats (oid 3375). grantor 10 is the bootstrap
-- superuser, matching the fresh-initdb GRANT in system_functions.sql.
INSERT INTO pg_catalog.pg_auth_members (roleid, member, grantor, admin_option)
  VALUES (3375, 8117, 10, false)
ON CONFLICT DO NOTHING;
