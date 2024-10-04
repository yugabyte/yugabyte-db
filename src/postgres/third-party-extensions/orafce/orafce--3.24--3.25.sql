CREATE OR REPLACE FUNCTION oracle.remainder(smallint, smallint)
RETURNS smallint AS 'MODULE_PATHNAME','orafce_reminder_smallint'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.remainder(int, int)
RETURNS int AS 'MODULE_PATHNAME','orafce_reminder_int'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.remainder(bigint, bigint)
RETURNS bigint AS 'MODULE_PATHNAME','orafce_reminder_bigint'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.remainder(numeric, numeric)
RETURNS numeric AS 'MODULE_PATHNAME','orafce_reminder_numeric'
LANGUAGE C IMMUTABLE;

DO $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 90600) THEN
    EXECUTE $_$ALTER FUNCTION oracle.remainder(smallint, smallint) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.remainder(int, int) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.remainder(bigint, bigint) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.remainder(numeric, numeric) PARALLEL SAFE$_$;
  END IF;
END;
$$;
