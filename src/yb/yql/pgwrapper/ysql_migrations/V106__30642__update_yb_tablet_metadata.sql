BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Update yb_get_tablet_metadata to include the decoded object oid along with
  -- start_range, end_range, tablet_attrs, and tablet_state.
  DELETE FROM pg_catalog.pg_proc WHERE oid = 8099;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes,
    proargnames,
    proargdefaults, protrftypes,
    prosrc, probin, prosqlbody, proconfig, proacl
  ) VALUES
    (8099, 'yb_get_tablet_metadata', 11, 10, 12,
     1, 100, 0, '-', 'f',
     false, false, true, true, 'v',
     'r', 0, 0, 2249, '',
     '{25,25,26,25,25,25,23,23,25,1009,25,25,114,25}', '{o,o,o,o,o,o,o,o,o,o,o,o,o,o}',
     '{tablet_id,object_uuid,oid,namespace,object_name,type,start_hash_code,end_hash_code,leader,replicas,start_range,end_range,tablet_attrs,tablet_state}',
     NULL, NULL,
     'yb_get_tablet_metadata', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;


BEGIN;
  DROP VIEW pg_catalog.yb_tablet_metadata;
  CREATE OR REPLACE VIEW pg_catalog.yb_tablet_metadata
    WITH (use_initdb_acl = true) AS
    SELECT
        t.tablet_id,
        -- Stable PG table oid (pg_class.oid) reported by the master; unlike the
        -- relfilenode, it survives table rewrites. NULL for non-YSQL tables
        -- (e.g. the 'transactions' table) and colocation parents.
        t.oid,
        t.namespace    AS db_name,
        t.object_name  AS relname,
        t.start_hash_code,
        t.end_hash_code,
        t.leader,
        t.replicas,
        t.start_range,
        t.end_range,
        t.tablet_attrs,
        t.tablet_state
    FROM
        yb_get_tablet_metadata() t
    WHERE
        -- Condition 1: Include the system 'transactions' table.
        (t.namespace = 'system' AND t.object_name = 'transactions')
    OR
        -- Condition 2: Include YSQL tables.
        (t.type = 'YSQL');
COMMIT;
