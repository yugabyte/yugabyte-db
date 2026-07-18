BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_attribute (
    attrelid, attname, atttypid, attstattarget, attlen, attnum, attndims, attcacheoff, atttypmod,
    attbyval, attalign, attstorage, attcompression, attnotnull, atthasdef, atthasmissing,
    attidentity, attgenerated, attisdropped, attislocal, attinhcount, attcollation, attacl,
    attoptions, attfdwoptions, attmissingval)
  SELECT
    oid, 'ybctid', 17, 0, -1, -7, 0, -1, -1, false, 'i', 'x', '', true, false, false, '', '',
    false, true, 0, 0, NULL, NULL, NULL, NULL
  FROM
    pg_catalog.pg_class
  WHERE
    relkind = 'r' AND relpersistence = 'p' AND
    NOT EXISTS (SELECT true FROM pg_catalog.pg_attribute
                WHERE attrelid = pg_catalog.pg_class.oid AND attname = 'ybctid');
COMMIT;
