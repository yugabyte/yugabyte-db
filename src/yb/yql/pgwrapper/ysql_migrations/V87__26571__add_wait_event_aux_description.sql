BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  -- Update function definition to include wait_event_aux_description column
  DELETE FROM pg_catalog.pg_proc WHERE proname = 'yb_wait_event_desc' AND
    pronamespace = 'pg_catalog'::regnamespace;
  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
    pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
    proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES (
    8069, 'yb_wait_event_desc', 11, 10, 12, 1, 100, 0, '-', 'f', false,
    false, true, true, 'i', 'r', 0, 0, 2249, '', '{25,25,25,25,20,25}',
    '{o,o,o,o,o,o}', '{wait_event_class,wait_event_type,wait_event,
    wait_event_description,wait_event_code,wait_event_aux_description}',
    NULL, NULL, 'yb_wait_event_desc', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;
COMMIT;

-- Recreate the system view to include the new column
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.yb_wait_event_desc'::regclass
          AND attname = 'wait_event_aux_description'
          AND NOT attisdropped
  ) THEN
    CREATE OR REPLACE VIEW pg_catalog.yb_wait_event_desc
    WITH (use_initdb_acl = true)
    AS
      SELECT *
      FROM yb_wait_event_desc();
  END IF;
END $$;

