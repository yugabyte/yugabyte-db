BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_tablegroup (
    grpname       name        NOT NULL,
    grpowner      oid         NOT NULL,
    grptablespace oid         NOT NULL,
    grpacl        aclitem[],
    grpoptions    text[],
    CONSTRAINT pg_yb_tablegroup_oid_index PRIMARY KEY (oid ASC)
      WITH (table_oid = 8037)
  ) WITH (
    oids = true,
    table_oid = 8036,
    row_type_oid = 8038
  );
COMMIT;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_yb_tablegroup (oid, grpname, grpowner, grptablespace, grpacl, grpoptions)
  SELECT oid, grpname, grpowner, 0, grpacl, grpoptions
  FROM pg_tablegroup
  ON CONFLICT DO NOTHING;
COMMIT;
