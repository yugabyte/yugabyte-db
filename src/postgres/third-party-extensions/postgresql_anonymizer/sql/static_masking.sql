
--
-- Wait for PGRX 0.12 rc to remove this function
--

SET search_path='';

-- Walk through all tables with a masked column and execute anonymize_table on them
CREATE OR REPLACE FUNCTION anon.anonymize_database(policy TEXT DEFAULT 'anon')
RETURNS BOOLEAN AS
$$
  SELECT pg_catalog.bool_or(anon.anonymize_table(t.regclass,policy))
  FROM (
      SELECT DISTINCT objoid as regclass
      FROM pg_catalog.pg_seclabel
      WHERE provider = quote_ident(policy)
      AND classoid='pg_class'::REGCLASS::OID
  ) as t;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because of UPDATE
  SECURITY INVOKER
;

SECURITY LABEL FOR anon ON FUNCTION anon.anonymize_database IS 'UNTRUSTED';

RESET search_path;
