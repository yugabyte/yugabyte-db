SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
    (8022, 'ybginhandler', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
     'v', 's', 1, 0, 325, '2281', NULL, NULL, NULL, NULL, NULL, 'ybginhandler', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_am (
  oid, amname, amhandler, amtype
) VALUES
  (8021, 'ybgin', 'ybginhandler', 'i')
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_opclass (
  oid, opcmethod, opcname, opcnamespace, opcowner, opcfamily, opcintype, opcdefault, opckeytype
) VALUES
  (8023, 8021, 'array_ops', 11, 10, 2745, 2277, true, 2283),
  (8024, 8021, 'tsvector_ops', 11, 10, 3659, 3614, true, 25),
  (8025, 8021, 'jsonb_ops', 11, 10, 4036, 3802, true, 25),
  (8026, 8021, 'jsonb_path_ops', 11, 10, 4037, 3802, false, 23)
ON CONFLICT DO NOTHING;

-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1255 AND refobjid = 8022
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      -- pg_proc
      (0, 0, 0, 1255, 8022, 0, 'p'),
      -- pg_am
      (0, 0, 0, 2601, 8021, 0, 'p'),
      -- pg_opclass
      (0, 0, 0, 2616, 8023, 0, 'p'),
      (0, 0, 0, 2616, 8024, 0, 'p'),
      (0, 0, 0, 2616, 8025, 0, 'p'),
      (0, 0, 0, 2616, 8026, 0, 'p');
  END IF;
END $$;
