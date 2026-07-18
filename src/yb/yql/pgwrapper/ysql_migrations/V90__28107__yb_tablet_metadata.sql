BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

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
    -- implementation of yb_get_tablet_metadata
    (8099, 'yb_get_tablet_metadata', 11, 10, 12,
     1, 100, 0, '-', 'f',
     false, false, true, true, 'v',
     'r', 0, 0, 2249, '',
     '{25,25,25,25,25,23,23,25,1009}', '{o,o,o,o,o,o,o,o,o}',
     '{tablet_id,object_uuid,namespace,object_name,type,start_hash_code,end_hash_code,leader,replicas}',
     NULL, NULL,
     'yb_get_tablet_metadata', NULL, NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8099, 1255, 0, 'Get metadata of all the tablets'
  ) ON CONFLICT DO NOTHING;
COMMIT;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_views
    WHERE viewname = 'yb_tablet_metadata'
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_tablet_metadata 
    WITH (use_initdb_acl = true) AS
        SELECT
            t.tablet_id,
            -- OID is NULL for the 'transactions' table, otherwise derived from the UUID.
            CASE
                WHEN t.namespace = 'system' AND t.object_name = 'transactions'
                    THEN NULL
                WHEN length(t.object_uuid) != 32
                    THEN NULL
                ELSE
                    ('x' || right(t.object_uuid, 8))::bit(32)::int::oid
            END AS oid,
            t.namespace    AS db_name,
            t.object_name  AS relname,
            t.start_hash_code,
            t.end_hash_code,
            t.leader,
            t.replicas
        FROM
            yb_get_tablet_metadata() t
        LEFT JOIN
            pg_class c ON c.relname = t.object_name
        LEFT JOIN
            pg_namespace n ON n.oid = c.relnamespace
        WHERE
            -- Condition 1: Include the system 'transactions' table.
            (t.namespace = 'system' AND t.object_name = 'transactions')
        OR
            -- Condition 2: Include user tables, while excluding system and catalog objects.
            (t.type = 'YSQL' AND n.nspname  NOT IN ('pg_catalog', 'information_schema'));
  END IF;
END $$;
