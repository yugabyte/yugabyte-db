SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_authid (
  oid, rolname, rolsuper, rolinherit, rolcreaterole, rolcreatedb, rolcanlogin,
  rolreplication, rolbypassrls, rolconnlimit, rolpassword, rolvaliduntil
) VALUES
  (8039, 'yb_db_admin', false, false, false, false, false, false, false,
   -1, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_shdepend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_shdepend
      WHERE refclassid = 1260 AND refobjid = 8039
  ) THEN
    INSERT INTO pg_catalog.pg_shdepend (
      dbid, classid, objid, objsubid, refclassid, refobjid, deptype
    ) VALUES (0, 0, 0, 0, 1260, 8039, 'p');
  END IF;
END $$;
