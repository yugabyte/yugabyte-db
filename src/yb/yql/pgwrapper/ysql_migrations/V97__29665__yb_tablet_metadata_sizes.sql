BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Add attributes column to yb_get_tablet_metadata().
  UPDATE pg_catalog.pg_proc SET
    proallargtypes = '{25,25,25,25,25,23,23,25,1009,3802}',
    proargmodes = '{o,o,o,o,o,o,o,o,o,o}',
    proargnames = '{tablet_id,object_uuid,namespace,object_name,type,start_hash_code,end_hash_code,leader,replicas,attributes}'
  WHERE proname = 'yb_get_tablet_metadata' AND pronamespace = 11;
COMMIT;

-- Recreate the view to include the new column.
CREATE OR REPLACE VIEW pg_catalog.yb_tablet_metadata
WITH (use_initdb_acl = true) AS
    SELECT
        t.tablet_id,
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
        t.replicas,
        t.attributes
    FROM
        yb_get_tablet_metadata() t
    LEFT JOIN
        pg_class c ON c.relname = t.object_name
    LEFT JOIN
        pg_namespace n ON n.oid = c.relnamespace
    WHERE
        (t.namespace = 'system' AND t.object_name = 'transactions')
    OR
        (t.type = 'YSQL' AND n.nspname NOT IN ('pg_catalog', 'information_schema'));
