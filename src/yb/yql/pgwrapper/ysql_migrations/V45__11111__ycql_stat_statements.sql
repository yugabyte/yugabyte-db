SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DELETE FROM pg_catalog.pg_proc WHERE proname = 'ycql_stat_statements' AND
        pronamespace = 'pg_catalog'::regnamespace;

INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
(8067, 'ycql_stat_statements', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, true,
 'v', 's', 0, 0, 2249, '', '{20,25,16,20,701,701,701,701,701}', '{o,o,o,o,o,o,o,o,o}',
 '{queryid,query,is_prepared,calls,total_time,min_time,max_time,mean_time,stddev_time}',
 NULL, NULL, 'ycql_stat_statements', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8067
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8067, 0, 'p');
END IF;
END $$;

CREATE OR REPLACE VIEW pg_catalog.ycql_stat_statements WITH (use_initdb_acl = true) AS
    SELECT * FROM ycql_stat_statements();
