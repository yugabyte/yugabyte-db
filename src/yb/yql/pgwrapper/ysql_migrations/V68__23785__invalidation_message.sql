BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_invalidation_messages(
    db_oid                oid    NOT NULL,
    current_version       bigint NOT NULL,
    message_time          bigint NOT NULL,
    messages              bytea,
    CONSTRAINT pg_yb_invalidation_messages_db_oid_current_version_index PRIMARY KEY (db_oid ASC, current_version ASC)
      WITH (table_oid = 8082)
  ) WITH (
    oids = false,
    table_oid = 8080,
    row_type_oid = 8081
  ) TABLESPACE pg_global;
COMMIT;

BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8083, 'yb_increment_db_catalog_version_with_inval_messages', 11, 10, 14, 3000, 0, 0, '-', 'f',
     false, false, false, false, 'v', 'u', 4, 0, 20, '26 16 17 23', NULL, NULL,
     '{db_oid,is_breaking_change,messages,expiration_sec}', NULL, NULL,
     'delete from pg_yb_invalidation_messages where pg_yb_invalidation_messages.db_oid = yb_increment_db_catalog_version_with_inval_messages.db_oid and message_time < extract(epoch from now())::bigint - expiration_sec; with changed_version as (update pg_yb_catalog_version set current_version = current_version + 1, last_breaking_version = case when is_breaking_change then current_version + 1 else last_breaking_version end where pg_yb_catalog_version.db_oid = yb_increment_db_catalog_version_with_inval_messages.db_oid returning pg_yb_catalog_version.db_oid, pg_yb_catalog_version.current_version) insert into pg_yb_invalidation_messages select changed_version.db_oid, current_version, extract(epoch from now())::bigint, messages from changed_version returning current_version',
     NULL, NULL, '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8083, 1255, 0, 'increments catalog version of one database with PG invalidation messages'
  ) ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8083, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, prosupport,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
  ) VALUES
    (8084, 'yb_increment_all_db_catalog_versions_with_inval_messages', 11, 10, 14, 6000, 0, 0, '-', 'f',
     false, false, false, false, 'v', 'u', 4, 0, 20, '26 16 17 23', NULL, NULL,
     '{db_oid,is_breaking_change,messages,expiration_sec}', NULL, NULL,
     'delete from pg_yb_invalidation_messages where message_time < extract(epoch from now())::bigint - expiration_sec; with changed_version as (update pg_yb_catalog_version set current_version = current_version + 1, last_breaking_version = case when is_breaking_change then current_version + 1 else last_breaking_version end returning pg_yb_catalog_version.db_oid, pg_yb_catalog_version.current_version) insert into pg_yb_invalidation_messages select changed_version.db_oid, current_version, extract(epoch from now())::bigint, messages from changed_version; select current_version from pg_yb_catalog_version where pg_yb_catalog_version.db_oid = yb_increment_all_db_catalog_versions_with_inval_messages.db_oid',
     NULL, NULL, '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8084, 1255, 0, 'increments catalog version of all databases with PG invalidation messages'
  ) ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_init_privs (
    objoid, classoid, objsubid, privtype, initprivs
  ) VALUES
    (8084, 1255, 0, 'i', '{postgres=X/postgres,yb_db_admin=X/postgres}')
  ON CONFLICT DO NOTHING;

COMMIT;
