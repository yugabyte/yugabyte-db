CREATE TABLE IF NOT EXISTS pg_catalog.pg_tablegroup (
  grpname    name        NOT NULL,
  grpowner   oid         NOT NULL,
  grpacl     aclitem[],
  grpoptions text[],
  CONSTRAINT pg_tablegroup_oid_index PRIMARY KEY (oid ASC)
    WITH (table_oid = 8001)
) WITH (
  oids = true,
  table_oid = 8000,
  row_type_oid = 8002
);
