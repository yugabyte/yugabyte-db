BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

    INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
      pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
      proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
    ) VALUES
      (8058, 'pg_get_backend_memory_contexts', 11, 10, 12, 1, 100, 0, '-', 'f', false, false, true, true,
      'v', 'r', 0, 0, 2249, '', '{25,25,25,23,20,20,20,20,20}', '{o,o,o,o,o,o,o,o,o}',
      '{name,ident,parent,level,total_bytes,total_nblocks,free_bytes,free_chunks,used_bytes}',
      NULL, NULL, 'pg_get_backend_memory_contexts', NULL, NULL, NULL)
    ON CONFLICT DO NOTHING;

    -- Create dependency records for everything we (possibly) created.
    -- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
    DO $$
    BEGIN
      IF NOT EXISTS (
        SELECT FROM pg_catalog.pg_depend
          WHERE refclassid = 1255 AND refobjid = 8058
      ) THEN
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) VALUES
          (0, 0, 0, 1255, 8058, 0, 'p');
      END IF;
    END $$;
COMMIT;

CREATE OR REPLACE VIEW pg_catalog.pg_backend_memory_contexts WITH (use_initdb_acl = true) AS
    SELECT * FROM pg_get_backend_memory_contexts();
