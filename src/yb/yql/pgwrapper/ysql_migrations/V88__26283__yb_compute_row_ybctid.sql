BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, proconfig, proacl
  ) VALUES
    -- implementation of yb_compute_row_ybctid
    (8098, 'yb_compute_row_ybctid', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, false, false, 'i',
     's', 3, 1, '17', '26 2249 17',
     NULL, NULL, '{relid,key_atts,ybidxbasectid}',
     '({CONST :consttype 17 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull true :location 107 :constvalue <>})',
     NULL, 'yb_compute_row_ybctid', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8098, 1255, 0, 'returns the ybctid given a relation and its key attributes'
  ) ON CONFLICT DO NOTHING;

  UPDATE pg_catalog.pg_proc SET provolatile='v', proargnames='{indexrelid,single_snapshot_mode}',
  proargdefaults='({CONST :consttype 16 :consttypmod -1 :constcollid 0 :constlen 1 :constbyval true :constisnull false :location 94 :constvalue 1 [ 0 0 0 0 0 0 0 0 ]})',
  proisstrict=false, proargtypes='26 16', pronargdefaults=1, pronargs=2 WHERE oid = 8090;

  UPDATE pg_catalog.pg_description SET description = 'checks whether the index is consistent with its base relation'
  WHERE objoid = 8090 AND classoid = 1255;

COMMIT;
