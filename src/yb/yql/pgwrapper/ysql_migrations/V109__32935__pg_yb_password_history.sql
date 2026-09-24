BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_password_history (
    pwdhstrole        oid         NOT NULL,
    pwdhstchangetime  timestamptz NOT NULL,
    pwdhstpassword    text        COLLATE "C" NOT NULL,
    CONSTRAINT pg_yb_password_history_pwdhstrole_pwdhstchangetime_index PRIMARY KEY (pwdhstrole ASC, pwdhstchangetime ASC)
      WITH (table_oid = 8120)
  ) WITH (
    oids = false,
    table_oid = 8118,
    row_type_oid = 8119
  ) TABLESPACE pg_global;

-- table stores password hashes, so it must not be publicly readable. 
REVOKE ALL ON pg_catalog.pg_yb_password_history FROM public;

COMMIT;

-- Record the grants above as initial privileges so pg_dump omits them
BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
  UPDATE pg_catalog.pg_init_privs ip
    SET initprivs = c.relacl
    FROM pg_catalog.pg_class c
    WHERE c.relnamespace = 'pg_catalog'::regnamespace
      AND c.relname = 'pg_yb_password_history'
      AND ip.objoid = c.oid
      AND ip.classoid = 'pg_catalog.pg_class'::regclass
      AND ip.objsubid = 0
      AND ip.privtype = 'i';
COMMIT;
