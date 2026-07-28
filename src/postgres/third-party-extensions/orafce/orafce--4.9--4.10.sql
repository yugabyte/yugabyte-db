DO $$
BEGIN
  IF NOT EXISTS(SELECT * FROM pg_roles where rolname = 'orafce_set_umask') THEN
    CREATE ROLE orafce_set_umask INHERIT NOLOGIN;
  END IF;
END;
$$;
