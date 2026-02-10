-- Migration to add built-in levenshtein functions for DocDB pushdown
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
      oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
      prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel,
      pronargs, pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes,
      proargnames, proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- levenshtein(text, text) -> int4
  (8102, 'levenshtein', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 2, 0, 23, '25 25', NULL, NULL, NULL, NULL, NULL,
   'yb_levenshtein', NULL, NULL, NULL),
  -- levenshtein(text, text, int4, int4, int4) -> int4
  (8103, 'levenshtein', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 5, 0, 23, '25 25 23 23 23', NULL, NULL, NULL, NULL, NULL,
   'yb_levenshtein_with_costs', NULL, NULL, NULL),
  -- levenshtein_less_equal(text, text, int4) -> int4
  (8104, 'levenshtein_less_equal', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 3, 0, 23, '25 25 23', NULL, NULL, NULL, NULL, NULL,
   'yb_levenshtein_less_equal', NULL, NULL, NULL),
  -- levenshtein_less_equal(text, text, int4, int4, int4, int4) -> int4
  (8105, 'levenshtein_less_equal', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 6, 0, 23, '25 25 23 23 23 23', NULL, NULL, NULL, NULL, NULL,
   'yb_levenshtein_less_equal_with_costs', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

-- Add function descriptions (match with pg_proc.dat fields)
INSERT INTO pg_catalog.pg_description (objoid, classoid, objsubid, description) VALUES
  (8102, 1255, 0, 'Levenshtein distance between two strings'),
  (8103, 1255, 0, 'Levenshtein distance with custom costs'),
  (8104, 1255, 0, 'Levenshtein distance with max limit'),
  (8105, 1255, 0, 'Levenshtein distance with custom costs and max limit')
ON CONFLICT DO NOTHING;

-- Note: No pg_depend entries needed, OIDs < 12000 are automatically pinned in PG15
