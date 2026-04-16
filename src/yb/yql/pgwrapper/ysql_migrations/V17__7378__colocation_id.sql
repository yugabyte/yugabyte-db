SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

-- TODO: Using WHERE oid = ... breaks something in YSQL, see #11105.
UPDATE pg_catalog.pg_proc SET
  proallargtypes = '{26,20,20,16,26,26}',
  proargmodes    = '{i,o,o,o,o,o}',
  proargnames    = '{table_oid,num_tablets,num_hash_key_columns,is_colocated,tablegroup_oid,colocation_id}'
WHERE proname = 'yb_table_properties' AND proargtypes = '26' AND
  pronamespace = 'pg_catalog'::regnamespace;

-- Replace tablegroup reloption with tablegroup_oid.
-- Note that this will skip pg_class rows with no reloptions, and we're fine with it.
WITH unnested_ro AS (
  SELECT
    c.oid,
    CASE
      WHEN o.option_name = 'tablegroup' THEN 'tablegroup_oid' ELSE o.option_name
    END as name,
    o.option_value AS val
  FROM pg_class c, pg_options_to_table(c.reloptions) o
  ORDER BY c.oid
),
changed_ro AS (
  SELECT
    unn.oid,
    ARRAY_AGG(unn.name || '=' || unn.val) AS reloptions
  FROM unnested_ro AS unn
  GROUP BY unn.oid
  ORDER BY unn.oid
)
UPDATE pg_class c
SET reloptions = changed_ro.reloptions
FROM changed_ro
WHERE c.oid = changed_ro.oid;

-- Force the cache refresh.
UPDATE pg_catalog.pg_yb_catalog_version SET current_version = current_version + 1;
