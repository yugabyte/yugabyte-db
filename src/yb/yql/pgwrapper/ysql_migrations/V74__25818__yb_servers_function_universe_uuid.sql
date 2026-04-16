BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_servers' AND 
  pronamespace = 'pg_catalog'::regnamespace;

INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  (8019, 'yb_servers', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, true,
   'v', 's', 0, 0, 2249, '', '{25,20,20,25,25,25,25,25,25,25}', 
   '{o,o,o,o,o,o,o,o,o,o}', 
   '{host,port,num_connections,node_type,cloud,region,zone,public_ip,uuid,universe_uuid}',
   NULL, NULL, 'yb_servers', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

COMMIT;
