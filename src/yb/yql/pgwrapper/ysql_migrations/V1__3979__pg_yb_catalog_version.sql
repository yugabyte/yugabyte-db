BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_catalog_version (
    db_oid                oid    NOT NULL,
    current_version       bigint NOT NULL,
    last_breaking_version bigint NOT NULL,
    CONSTRAINT pg_yb_catalog_version_db_oid_index PRIMARY KEY (db_oid ASC)
      WITH (table_oid = 8012)
  ) WITH (
    oids = false,
    table_oid = 8010,
    row_type_oid = 8011
  ) TABLESPACE pg_global;
COMMIT;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8029, 'yb_catalog_version', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
     'v', 's', 0, 0, 20, '', NULL, NULL, NULL, NULL, NULL, 'yb_catalog_version', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  -- Create dependency records for everything we (possibly) created.
  -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
  DO $$
  BEGIN
    IF NOT EXISTS (
      SELECT FROM pg_catalog.pg_depend
        WHERE refclassid = 1255 AND refobjid = 8029
    ) THEN
      INSERT INTO pg_catalog.pg_depend (
        classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
      ) VALUES
        (0, 0, 0, 1255, 8029, 0, 'p');
    END IF;
  END $$;

  -- Insert a version which would immediately follow the one we track using legacy approach.
  WITH catalog_version AS (
    SELECT pg_catalog.yb_catalog_version() AS v
  )
  INSERT INTO pg_yb_catalog_version(
    db_oid, current_version, last_breaking_version
  ) SELECT 1, v, v FROM catalog_version
  ON CONFLICT DO NOTHING;
COMMIT;
