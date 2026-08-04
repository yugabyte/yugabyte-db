
BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_logical_client_version (
    db_oid                oid    NOT NULL,
    current_version       bigint NOT NULL,
    CONSTRAINT pg_yb_logical_client_version_db_oid_index PRIMARY KEY (db_oid ASC)
      WITH (table_oid = 8075)
  ) WITH (
    oids = false,
    table_oid = 8073,
    row_type_oid = 8074
  ) TABLESPACE pg_global;
COMMIT;


BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  INSERT INTO pg_yb_logical_client_version(db_oid, current_version)
  SELECT oid, 1 FROM pg_database WHERE
  oid NOT IN (SELECT db_oid from pg_catalog.pg_yb_logical_client_version);
COMMIT;
