DO $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 130000) THEN
    EXECUTE $_$ALTER TYPE oracle.nvarchar2 SET (storage = extended)$_$;
    EXECUTE $_$ALTER TYPE oracle.varchar2 SET (storage = extended)$_$;
  ELSE
    UPDATE pg_type SET typstorage = 'x' WHERE typname = 'nvarchar2' AND typnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'oracle');
    UPDATE pg_type SET typstorage = 'x' WHERE typname = 'varchar2' AND typnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'oracle');
  END IF;
END;
$$;
