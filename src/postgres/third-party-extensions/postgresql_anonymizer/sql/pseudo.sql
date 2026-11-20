-------------------------------------------------------------------------------
-- Pseudonymized data
-------------------------------------------------------------------------------

SET search_path='';

--
-- All pseudonymizing functions are declared as `RESTRICTED` which means they
-- are `TRUSTED` but masked users can't call them directly.
-- This is important to prevent a rogue masked user from brute forcing the
-- function and reveal the secret salt
--

--
-- Convert an hexadecimal value to an integer
--
CREATE OR REPLACE FUNCTION anon.hex_to_int(
  hexval TEXT
)
RETURNS INT AS $$
DECLARE
    result  INT;
BEGIN
    EXECUTE 'SELECT x' || quote_literal(hexval) || '::INT' INTO result;
    RETURN result;
END;
$$
  LANGUAGE plpgsql
  IMMUTABLE
  STRICT
  PARALLEL SAFE
  SECURITY INVOKER
  SET search_path=''
;

--
-- Return a deterministic value inside a range of OID for a given seed+salt
--
CREATE OR REPLACE FUNCTION anon.projection_to_oid(
  seed ANYELEMENT,
  salt TEXT,
  last_oid BIGINT
)
RETURNS INT AS $$
  --
  -- get a md5 hash of the seed and then project it on a 0-to-1 scale
  -- then multiply by the latest oid
  -- which give a deterministic oid inside the range
  --
  -- This works because MD5 signatures values have a uniform distribution
  -- see https://crypto.stackexchange.com/questions/14967/distribution-for-a-subset-of-md5
  --
  SELECT CAST(
    -- we use only the 6 first characters of the md5 signature
    -- and we divide by the max value : x'FFFFFF' = 16777215
    last_oid * anon.hex_to_int(md5(seed::TEXT||salt)::char(6)) / 16777215.0
  AS INT )
$$
  LANGUAGE SQL
  IMMUTABLE
  RETURNS NULL ON NULL INPUT
  PARALLEL SAFE
  SECURITY INVOKER
  SET search_path=''
;


--
-- the pseudo function are declared as SECURITY DEFINER because the access
-- the anon.salt which is only visible to superusers.
--
-- If a masked role can read the salt, he/she can run a brute force attack to
-- retrieve the original data based on the pseudonymized data
--

CREATE OR REPLACE FUNCTION anon.pseudo_first_name(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.first_name
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT max(oid) FROM anon.first_name)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path = pg_catalog,pg_temp
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_first_name(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_last_name(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.last_name
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT max(oid) FROM anon.last_name)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path = pg_catalog,pg_temp
;


SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_last_name(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_email(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.email
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.email)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_email(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_city(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.city
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.city)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_city(ANYELEMENT,TEXT)
  IS 'RESTRICTED';


CREATE OR REPLACE FUNCTION anon.pseudo_country(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.country
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.country)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_country(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_company(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.company
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.company)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_company(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_iban(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.iban
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.iban)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_iban(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.pseudo_siret(
  seed ANYELEMENT,
  salt TEXT DEFAULT NULL
)
RETURNS TEXT AS $$
  SELECT COALESCE(val,anon.notice_if_not_init())
  FROM anon.siret
  WHERE oid = anon.projection_to_oid(
    seed,
    COALESCE(salt, pg_catalog.current_setting('anon.salt')),
    (SELECT MAX(oid) FROM anon.siret)
  );
$$
  LANGUAGE SQL
  STABLE
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_siret(ANYELEMENT,TEXT)
  IS 'RESTRICTED';

-------------------------------------------------------------------------------
--- Shifting
-------------------------------------------------------------------------------

CREATE OR REPLACE FUNCTION anon.pseudo_shift(i BIGINT)
RETURNS BIGINT AS $$
  -- Convert input to numeric to avoid "out of range" errors
  SELECT (i::NUMERIC+pg_catalog.current_setting('anon.shift')::INT) % 9223372036854775807;
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL SAFE
  SECURITY DEFINER -- in order to get the secret shift value
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_shift(BIGINT)
  IS 'RESTRICTED';


CREATE OR REPLACE FUNCTION anon.pseudo_xor(i BIGINT)
RETURNS BIGINT AS $$
  SELECT i # pg_catalog.current_setting('anon.shift')::INT ;
$$
  LANGUAGE SQL
  VOLATILE
  RETURNS NULL ON NULL INPUT
  PARALLEL SAFE
  SECURITY DEFINER -- in order to get the secret shift value
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.pseudo_xor(BIGINT)
  IS 'RESTRICTED';

CREATE OR REPLACE FUNCTION anon.set_shift(val INT DEFAULT NULL)
RETURNS BOOL AS $$
DECLARE
    shift INT;
BEGIN
    SELECT COALESCE(val, pg_catalog.random()*2147483647) INTO shift;
    EXECUTE 'ALTER DATABASE ' || pg_catalog.quote_ident(current_database()) || ' SET anon.shift TO ' || shift;

    EXECUTE 'SET anon.shift TO ' || shift;
    RETURN TRUE;
END;
$$
  LANGUAGE plpgsql
  VOLATILE
  PARALLEL SAFE
  SECURITY DEFINER -- in order to get the secret shift value
  SET search_path=''
;

-- Set a random value to the shift when the extension is loaded
SELECT anon.set_shift();

RESET search_path;
