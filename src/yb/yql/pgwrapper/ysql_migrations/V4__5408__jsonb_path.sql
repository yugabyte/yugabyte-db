SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;


INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- Inserting I/O procedures for types before inserting types.
  (4001, 'jsonpath_in', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 1, 0, 8003, '2275', NULL, NULL, NULL, NULL, NULL, 'jsonpath_in', NULL, NULL, NULL),
  (4002, 'jsonpath_recv', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 1, 0, 8003, '2281', NULL, NULL, NULL, NULL, NULL, 'jsonpath_recv', NULL, NULL, NULL),
  (4003, 'jsonpath_out', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 1, 0, 2275, '8003', NULL, NULL, NULL, NULL, NULL, 'jsonpath_out', NULL, NULL, NULL),
  (4004, 'jsonpath_send', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 1, 0, 17, '8003', NULL, NULL, NULL, NULL, NULL, 'jsonpath_send', NULL, NULL, NULL),

  -- implementation of @? operator
  (4010, 'jsonb_path_exists_opr', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 2, 0, 16, '3802 8003', NULL, NULL,
   NULL, NULL, NULL, 'jsonb_path_exists_opr', NULL, NULL, NULL),

  -- implementation of @@ operator
  (4011, 'jsonb_path_match_opr', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 2, 0, 16, '3802 8003', NULL, NULL,
   NULL, NULL, NULL, 'jsonb_path_match_opr', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;


INSERT INTO pg_catalog.pg_type (
  oid, typname, typnamespace, typowner, typlen, typbyval, typtype, typcategory,
  typispreferred, typisdefined, typdelim, typrelid, typelem, typarray,
  typinput, typoutput, typreceive, typsend, typmodin, typmodout, typanalyze,
  typalign, typstorage, typnotnull, typbasetype, typtypmod, typndims, typcollation, typdefaultbin,
  typdefault, typacl
) VALUES
  (8003, 'jsonpath', 11, 10, -1, false, 'b', 'U', false, true, ',', 0, 0, 8004,
   'jsonpath_in', 'jsonpath_out', 'jsonpath_recv', 'jsonpath_send', '-', '-', '-',
   'i', 'x', false, 0, -1, 0, 0, NULL, NULL, NULL),
  (8004, '_jsonpath', 11, 10, -1, false, 'b', 'A', false, true, ',', 0, 8003, 0,
   'array_in', 'array_out', 'array_recv', 'array_send', '-', '-', 'array_typanalyze',
   'i', 'x', false, 0, -1, 0, 0, NULL, NULL, NULL)
ON CONFLICT DO NOTHING;


INSERT INTO pg_catalog.pg_operator (
  oid, oprname, oprnamespace, oprowner, oprkind, oprcanmerge, oprcanhash, oprleft, oprright,
  oprresult, oprcom, oprnegate, oprcode, oprrest, oprjoin
) VALUES
  (4012, '@?', 11, 10, 'b', false, false, 3802, 8003, 16, 0, 0,
   'jsonb_path_exists_opr', 'contsel', 'contjoinsel'),
  (4013, '@@', 11, 10, 'b', false, false, 3802, 8003, 16, 0, 0,
   'jsonb_path_match_opr', 'contsel', 'contjoinsel')
ON CONFLICT DO NOTHING;


INSERT INTO pg_catalog.pg_amop (
  amopfamily, amoplefttype, amoprighttype, amopstrategy, amoppurpose, amopopr, amopmethod,
  amopsortfamily
) VALUES
  (4036, 3802, 8003, 15, 's', 4012, 2742, 0),
  (4036, 3802, 8003, 16, 's', 4013, 2742, 0),
  (4037, 3802, 8003, 15, 's', 4012, 2742, 0),
  (4037, 3802, 8003, 16, 's', 4013, 2742, 0)
ON CONFLICT DO NOTHING;


INSERT INTO pg_catalog.pg_proc (
  oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
  prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
  pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
  proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
  -- json_path_* functions
  (4005, 'jsonb_path_exists', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 4, 2, 16, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 95 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 144 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_exists', NULL, NULL, NULL),
  (4006, 'jsonb_path_query', 11, 10, 12, 1, 1000, 0, '-', 'f', false, false, true, true,
   'i', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 94 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 142 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query', NULL, NULL, NULL),
  (4007, 'jsonb_path_query_array', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 100 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 154 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query_array', NULL, NULL, NULL),
  (4008, 'jsonb_path_query_first', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 100 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 154 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query_first', NULL, NULL, NULL),
  (4009, 'jsonb_path_match', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   'i', 's', 4, 2, 16, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 94 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 142 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_match', NULL, NULL, NULL),

  -- json_path_*_tz functions
  (8005, 'jsonb_path_exists_tz', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 4, 2, 16, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 98 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 147 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_exists_tz', NULL, NULL, NULL),
  (8006, 'jsonb_path_query_tz', 11, 10, 12, 1, 1000, 0, '-', 'f', false, false, true, true,
   's', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 97 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 145 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query_tz', NULL, NULL, NULL),
  (8007, 'jsonb_path_query_array_tz', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 103 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 157 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query_array_tz', NULL, NULL, NULL),
  (8008, 'jsonb_path_query_first_tz', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 4, 2, 3802, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 103 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 157 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
   NULL, 'jsonb_path_query_first_tz', NULL, NULL, NULL),
  (8009, 'jsonb_path_match_tz', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
   's', 's', 4, 2, 16, '3802 8003 3802 16', NULL, NULL,
   '{target,path,vars,silent}',
   '({CONST :consttype 3802 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull false :location 97 :constvalue 8 [ 32 0 0 0 0 0 0 32 ]} {CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 145 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})', NULL, 'jsonb_path_match_tz', NULL, NULL, NULL),

  -- Set part of a jsonb, handle NULL value
  (8945, 'jsonb_set_lax', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, false, false,
   'i', 's', 5, 2, 3802, '3802 1009 3802 16 25', NULL, NULL,
   '{jsonb_in,path,replacement,create_if_missing,null_value_treatment}',
   '({CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 138 :constvalue 1 [ 1 0 0 0 0 0 0 0 ]} {CONST :consttype 25 :consttypmod -1 :constcollid 100 :constlen -1 :constbyval false :constisnull false :location 190 :constvalue 17 [ 68 0 0 0 117 115 101 95 106 115 111 110 95 110 117 108 108 ]})',
   NULL, 'jsonb_set_lax', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;


-- Create dependency records for everything we (possibly) created.
-- Since pg_depend has no OID or unique constraint, using PL/pgSQL instead.
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend
      WHERE refclassid = 1247 AND refobjid = 8003
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      -- pg_type
      (0, 0, 0, 1247, 8003, 0, 'p'),
      (0, 0, 0, 1247, 8004, 0, 'p'),
      -- pg_operator
      (0, 0, 0, 2617, 4012, 0, 'p'),
      (0, 0, 0, 2617, 4013, 0, 'p'),
       -- pg_proc
      (0, 0, 0, 1255, 4001, 0, 'p'),
      (0, 0, 0, 1255, 4002, 0, 'p'),
      (0, 0, 0, 1255, 4003, 0, 'p'),
      (0, 0, 0, 1255, 4004, 0, 'p'),
      (0, 0, 0, 1255, 4005, 0, 'p'),
      (0, 0, 0, 1255, 4006, 0, 'p'),
      (0, 0, 0, 1255, 4007, 0, 'p'),
      (0, 0, 0, 1255, 4008, 0, 'p'),
      (0, 0, 0, 1255, 4009, 0, 'p'),
      (0, 0, 0, 1255, 4010, 0, 'p'),
      (0, 0, 0, 1255, 4011, 0, 'p'),
      (0, 0, 0, 1255, 8005, 0, 'p'),
      (0, 0, 0, 1255, 8006, 0, 'p'),
      (0, 0, 0, 1255, 8007, 0, 'p'),
      (0, 0, 0, 1255, 8008, 0, 'p'),
      (0, 0, 0, 1255, 8009, 0, 'p'),
      (0, 0, 0, 1255, 8945, 0, 'p');

    -- pg_amop (dynamic OIDs)
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) SELECT 0, 0, 0, 2602, oid, 0, 'p' FROM pg_catalog.pg_amop
      WHERE amopopr IN (4012, 4013);
  END IF;
END $$;
