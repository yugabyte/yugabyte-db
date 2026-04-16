BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_profile (
    prfname NAME NOT NULL,
    prfmaxfailedloginattempts INTEGER NOT NULL,
    prfpasswordlocktime INTEGER NOT NULL,
    CONSTRAINT pg_yb_profile_oid_index PRIMARY KEY(oid ASC)
        WITH (table_oid = 8052),
    CONSTRAINT pg_yb_profile_prfname_index UNIQUE (prfname)
        WITH (table_oid = 8057)
  ) WITH (
    oids = true,
    table_oid = 8051,
    row_type_oid = 8053
  ) TABLESPACE pg_global;

  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_role_profile (
    rolprfrole OID NOT NULL,
    rolprfprofile OID NOT NULL,
    rolprfstatus "char" NOT NULL,
    rolprffailedloginattempts INTEGER NOT NULL,
    rolprflockeduntil TIMESTAMPTZ NOT NULL,
    CONSTRAINT pg_yb_role_profile_oid_index PRIMARY KEY(oid ASC)
        WITH (table_oid = 8055)
  ) WITH (
    oids = true,
    table_oid = 8054,
    row_type_oid = 8056
  ) TABLESPACE pg_global;
COMMIT;
