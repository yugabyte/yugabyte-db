-------------------------------------------------------------------------------
-- Functions : INIT / RESET
-------------------------------------------------------------------------------

SET search_path='';

-- ADD unit tests in tests/sql/init.sql

CREATE OR REPLACE FUNCTION anon.load_csv(
  dest_table REGCLASS,
  csv_file TEXT
)
RETURNS BOOLEAN AS
$$
DECLARE
  csv_file_check TEXT;
  sequence TEXT;
BEGIN
-- This check does not work with PG 10 and below (absolute path not supported)
--
--  SELECT * INTO  csv_file_check
--  FROM pg_catalog.pg_stat_file(csv_file, missing_ok := TRUE );
--
--  IF csv_file_check IS NULL THEN
--    RAISE NOTICE 'Data file ''%'' is not present. Skipping.', csv_file;
--    RETURN FALSE;
--  END IF;

  -- load the csv file
  EXECUTE 'COPY ' || dest_table
      || ' FROM ' || quote_literal(csv_file);

  -- update the oid sequence (if any)
  SELECT pg_catalog.pg_get_serial_sequence(dest_table::TEXT,'oid')
  INTO sequence
  FROM pg_catalog.pg_attribute
  WHERE attname ='oid'
  AND attrelid = dest_table;

  IF sequence IS NOT NULL
  THEN
    EXECUTE format( 'SELECT pg_catalog.setval(%L, max(oid)) FROM %s',
                    sequence,
                    dest_table
    );
  END IF;

  -- clustering the table for better performance
/* YB(#1124): CLUSTER unsupported on YugabyteDB, no-op on DocDB storage.
  EXECUTE 'CLUSTER ' || dest_table;
*/ -- YB

  RETURN TRUE;

EXCEPTION

  WHEN undefined_file THEN
    RAISE NOTICE 'Data file ''%'' is not present. Skipping.', csv_file;
    RETURN FALSE;

  WHEN bad_copy_file_format THEN
    RAISE NOTICE 'Data file ''%'' has a bad CSV format. Skipping.', csv_file;
    RETURN FALSE;

  WHEN invalid_text_representation THEN
    RAISE NOTICE 'Data file ''%'' has a bad CSV format. Skipping.', csv_file;
    RETURN FALSE;

END;
$$
  LANGUAGE plpgsql
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL UNSAFE -- because of the EXCEPTION
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.load_csv IS 'UNTRUSTED';

-- load fake data from a given path
CREATE OR REPLACE FUNCTION anon.init(
  datapath TEXT
)
RETURNS BOOLEAN
AS $$
DECLARE
  datapath_check TEXT;
  success BOOLEAN;
BEGIN

  IF anon.is_initialized() THEN
    RAISE NOTICE 'The anon extension is already initialized.';
    RETURN TRUE;
  END IF;

  SELECT bool_or(results) INTO success
  FROM unnest(array[
    anon.load_csv('anon.identifiers_category',datapath||'/identifiers_category.csv'),
    anon.load_csv('anon.identifier',datapath ||'/identifier.csv'),
    anon.load_csv('anon.address',datapath ||'/address.csv'),
    anon.load_csv('anon.city',datapath ||'/city.csv'),
    anon.load_csv('anon.company',datapath ||'/company.csv'),
    anon.load_csv('anon.country',datapath ||'/country.csv'),
    anon.load_csv('anon.email', datapath ||'/email.csv'),
    anon.load_csv('anon.first_name',datapath ||'/first_name.csv'),
    anon.load_csv('anon.iban',datapath ||'/iban.csv'),
    anon.load_csv('anon.last_name',datapath ||'/last_name.csv'),
    anon.load_csv('anon.postcode',datapath ||'/postcode.csv'),
    anon.load_csv('anon.siret',datapath ||'/siret.csv'),
    anon.load_csv('anon.lorem_ipsum',datapath ||'/lorem_ipsum.csv')
  ]) results;
  RETURN success;

END;
$$
  LANGUAGE PLPGSQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL UNSAFE -- because load_csv is unsafe
  SECURITY INVOKER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.init(TEXT) IS 'UNTRUSTED';

-- People tend to forget the anon.init() step
-- This is a friendly notice for them
CREATE OR REPLACE FUNCTION anon.notice_if_not_init()
RETURNS TEXT AS
$$
BEGIN
  IF NOT anon.is_initialized() THEN
    RAISE NOTICE 'The anon extension is not initialized.'
      USING HINT='Use ''SELECT anon.init()'' before running this function';
  END IF;
  RETURN NULL;
END;
$$
  LANGUAGE plpgsql
  STABLE
  PARALLEL SAFE
  SECURITY INVOKER
  SET search_path='';
;
SECURITY LABEL FOR anon ON FUNCTION anon.notice_if_not_init IS 'UNTRUSTED';

-- load() is here for backward compatibility with version 0.6
CREATE OR REPLACE FUNCTION anon.load(TEXT)
RETURNS BOOLEAN AS
$$
  SELECT anon.init($1);
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL UNSAFE -- because init() is unsafe
  SECURITY INVOKER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.load(TEXT) IS 'UNTRUSTED';

-- If no path given, use the default data
CREATE OR REPLACE FUNCTION anon.init()
RETURNS BOOLEAN
AS $$
  WITH conf AS (
        -- find the local extension directory
        SELECT setting AS sharedir
        FROM pg_catalog.pg_config
        WHERE name = 'SHAREDIR'
    )
  SELECT anon.init(conf.sharedir || '/extension/anon/')
  FROM conf;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because init is unsafe
  SECURITY INVOKER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.init() IS 'UNTRUSTED';

-- load() is here for backward compatibility with version 0.6 and below
CREATE OR REPLACE FUNCTION anon.load()
RETURNS BOOLEAN
AS $$
BEGIN
  RAISE NOTICE 'anon.load() will be deprecated in future versions.'
    USING HINT = 'you should use anon.init() instead.';
  RETURN anon.init();
END;
$$
  LANGUAGE plpgsql
  VOLATILE
  PARALLEL UNSAFE -- because init is unsafe
  SECURITY INVOKER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.load() IS 'UNTRUSTED';

-- True if the fake data is already here
CREATE OR REPLACE FUNCTION anon.is_initialized()
RETURNS BOOLEAN
AS $$
  SELECT count(*)::INT::BOOLEAN
  FROM (   SELECT 1 FROM anon.address
     UNION SELECT 1 FROM anon.city
     UNION SELECT 1 FROM anon.company
     UNION SELECT 1 FROM anon.country
     UNION SELECT 1 FROM anon.email
     UNION SELECT 1 FROM anon.first_name
     UNION SELECT 1 FROM anon.iban
     UNION SELECT 1 FROM anon.last_name
     UNION SELECT 1 FROM anon.lorem_ipsum
     UNION SELECT 1 FROM anon.postcode
     UNION SELECT 1 FROM anon.siret
     -- ADD NEW TABLE HERE
     LIMIT 1
  ) t
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.is_initialized IS 'UNTRUSTED';

-- remove all fake data
CREATE OR REPLACE FUNCTION anon.reset()
RETURNS BOOLEAN AS
$$
    TRUNCATE anon.address;
    TRUNCATE anon.city;
    TRUNCATE anon.company;
    TRUNCATE anon.country;
    TRUNCATE anon.email;
    TRUNCATE anon.first_name;
    TRUNCATE anon.iban;
    TRUNCATE anon.last_name;
    TRUNCATE anon.lorem_ipsum;
    TRUNCATE anon.postcode;
    TRUNCATE anon.siret;
    TRUNCATE anon.identifiers_category CASCADE;
    TRUNCATE anon.identifier;
    -- ADD NEW TABLE HERE
    SELECT TRUE;
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because of TRUNCATE
  SECURITY INVOKER
  SET search_path=''
;
SECURITY LABEL FOR anon ON FUNCTION anon.reset IS 'UNTRUSTED';

-- backward compatibility with version 0.6 and below
CREATE OR REPLACE FUNCTION anon.unload()
RETURNS BOOLEAN AS
$$
  SELECT anon.reset()
$$
  LANGUAGE SQL
  VOLATILE
  PARALLEL UNSAFE -- because reset is unsafe
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.unload IS 'UNTRUSTED';

RESET search_path;
