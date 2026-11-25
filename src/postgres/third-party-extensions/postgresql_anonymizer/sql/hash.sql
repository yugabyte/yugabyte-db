-------------------------------------------------------------------------------
--- Generic hashing
-------------------------------------------------------------------------------

SET search_path='';

-- Return the hash of a value for a given algorithm and a salt
-- Standard algorithms are md5, sha224, sha256, sha384 and sha512
--
-- * In version 1.x, this was a wrapper around pgcrypto's digest() function
-- * Since version 2.x, `sha1` is not longer supported
--
-- /!\ This function will fail when the val or salt contains an unescaped character
-- because of the BYTEA conversion. We are NOT going to support unescaped character.
-- In most situation, this is the sign of a bug in the application, generally
-- when data input is not sanitized properly.
--
-- Users who really want to mask unescaped characters with this function should
-- disable the `standard_conforming_strings` parameter
--
-- More on this: https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/539
--
CREATE OR REPLACE FUNCTION anon.digest(
  val TEXT,
  salt TEXT,
  algorithm TEXT
)
RETURNS TEXT AS
$$
  SELECT CASE
    WHEN algorithm = 'md5'
      THEN pg_catalog.md5(concat(val,salt))
    WHEN algorithm = 'sha224'
      THEN pg_catalog.encode(pg_catalog.sha224(concat(val,salt)::BYTEA),'hex')
    WHEN algorithm = 'sha256'
      THEN pg_catalog.encode(pg_catalog.sha256(concat(val,salt)::BYTEA),'hex')
    WHEN algorithm = 'sha384'
      THEN pg_catalog.encode(pg_catalog.sha384(concat(val,salt)::BYTEA),'hex')
    WHEN algorithm = 'sha512'
      THEN pg_catalog.encode(pg_catalog.sha512(concat(val,salt)::BYTEA),'hex')
    ELSE NULL
    END
$$
  LANGUAGE SQL
  IMMUTABLE
  RETURNS NULL ON NULL INPUT
  PARALLEL SAFE
  SECURITY INVOKER
  SET search_path=''
;

SECURITY LABEL FOR anon ON FUNCTION anon.digest(TEXT, TEXT, TEXT)
  IS 'RESTRICTED';

--
-- Return a hash value for a seed
--
-- The function is a SECURITY DEFINER because `anon.salt` and `anon.algorithm`
-- are visible only to superusers.
-- see https://www.postgresql.org/docs/current/sql-createfunction.html#SQL-CREATEFUNCTION-SECURITY
--
CREATE OR REPLACE FUNCTION anon.hash(
  seed TEXT
)
RETURNS TEXT AS $$
  SELECT anon.digest(
    seed,
    pg_catalog.current_setting('anon.salt'),
    pg_catalog.current_setting('anon.algorithm')
  );
$$
  LANGUAGE SQL
  STABLE
  RETURNS NULL ON NULL INPUT
  PARALLEL SAFE
  SECURITY DEFINER
  SET search_path = ''
;

SECURITY LABEL FOR anon ON FUNCTION anon.hash(TEXT)
  IS 'RESTRICTED';

RESET search_path;
